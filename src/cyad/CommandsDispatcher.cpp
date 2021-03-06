/*
 * Copyright (c) 2014-2015 Samsung Electronics Co., Ltd All Rights Reserved
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */
/**
 * @file        src/cyad/CommandsDispatcher.cpp
 * @author      Aleksander Zdyb <a.zdyb@samsung.com>
 * @version     1.0
 * @brief       CommandsDispatcher class (implementation)
 */

#include <cynara-admin-types.h>
#include <cynara-error.h>
#include <cynara-policy-types.h>

#include <exceptions/BucketRecordCorruptedException.h>

#include <cyad/AdminLibraryInitializationFailedException.h>
#include <cyad/AdminPolicyParser.h>
#include <cyad/CommandlineParser/CmdlineOpts.h>
#include <cyad/CommandlineParser/PolicyParsingException.h>
#include <cyad/CynaraAdminPolicies.h>

#include "CommandsDispatcher.h"

namespace Cynara {

CommandsDispatcher::CommandsDispatcher(BaseDispatcherIO &io, BaseAdminApiWrapper &adminApiWrapper,
                                       BaseErrorApiWrapper &errorApiWrapper)
    : m_io(io), m_adminApiWrapper(adminApiWrapper), m_errorApiWrapper(errorApiWrapper),
      m_cynaraAdmin(nullptr)
{
    auto ret = m_adminApiWrapper.cynara_admin_initialize(&m_cynaraAdmin);
    if (ret != CYNARA_API_SUCCESS) {
        printAdminApiError(ret);
        throw AdminLibraryInitializationFailedException(ret);
    }

    try {
        initPolicyTranslator();
    } catch (int ret) {
        printAdminApiError(ret);
        // It's not really initialization error, but still nothing depends on user input
        throw AdminLibraryInitializationFailedException(ret);
    }
}

CommandsDispatcher::~CommandsDispatcher() {
    auto ret = m_adminApiWrapper.cynara_admin_finish(m_cynaraAdmin);
    if (ret != CYNARA_API_SUCCESS)
        printAdminApiError(ret);
}

int CommandsDispatcher::execute(CyadCommand &) {
    m_io.cerr() << "Whatever you wanted, it's not implemented" << std::endl;
    return CYNARA_API_UNKNOWN_ERROR;
}

int CommandsDispatcher::execute(HelpCyadCommand &) {
    m_io.cout() << CmdlineOpts::makeHelp() << std::endl;
    return CYNARA_API_SUCCESS;
}

int CommandsDispatcher::execute(ErrorCyadCommand &result) {
    m_io.cerr() << "There was an error in command-line options:" << std::endl;
    m_io.cerr() << result.message() << std::endl;

    m_io.cerr() << std::endl << CmdlineOpts::makeHelp() << std::endl;
    return CYNARA_API_INVALID_COMMANDLINE_PARAM;
}

int CommandsDispatcher::execute(DeleteBucketCyadCommand &result) {
    auto ret = m_adminApiWrapper.cynara_admin_set_bucket(m_cynaraAdmin, result.bucketId().c_str(),
                                                         CYNARA_ADMIN_DELETE, nullptr);

    if (ret != CYNARA_API_SUCCESS)
        printAdminApiError(ret);

    return ret;
}

int CommandsDispatcher::execute(SetBucketCyadCommand &result) {
    const auto &policyResult = result.policyResult();
    const char *metadata = policyResult.metadata().empty() ? nullptr
                                                           : policyResult.metadata().c_str();

    try {
        int policyType = m_policyTranslator.translate(result.policyResult().policyType());
        auto ret = m_adminApiWrapper.cynara_admin_set_bucket(m_cynaraAdmin,
                                                             result.bucketId().c_str(),
                                                             policyType, metadata);

        if (ret != CYNARA_API_SUCCESS)
            printAdminApiError(ret);

        return ret;
    } catch (const PolicyParsingException &) {
        m_io.cerr() << "Invalid policy" << std::endl;
        return CYNARA_API_INVALID_COMMANDLINE_PARAM;
    }
}

int CommandsDispatcher::execute(SetPolicyCyadCommand &result) {
    CynaraAdminPolicies policies;

    try {
        int policyType = m_policyTranslator.translate(result.policyResult().policyType());
        policies.add(result.bucketId(), PolicyResult(policyType, result.policyResult().metadata()),
                     result.policyKey());
        policies.seal();
    } catch (const PolicyParsingException &) {
        m_io.cerr() << "Invalid policy" << std::endl;
        return CYNARA_API_INVALID_COMMANDLINE_PARAM;
    }

    auto ret = m_adminApiWrapper.cynara_admin_set_policies(m_cynaraAdmin, policies.data());

    if (ret != CYNARA_API_SUCCESS)
        printAdminApiError(ret);

    return ret;
}

int CommandsDispatcher::execute(SetPolicyBulkCyadCommand &result) {
    auto input = m_io.openFile(result.filename());

    try {
        using Cynara::AdminPolicyParser::parse;
        auto policies = parse(*input, std::bind(&PolicyTypeTranslator::translate,
                                               &m_policyTranslator, std::placeholders::_1));
        auto ret = m_adminApiWrapper.cynara_admin_set_policies(m_cynaraAdmin, policies.data());
        if (ret != CYNARA_API_SUCCESS)
            printAdminApiError(ret);
        return ret;
    } catch (const BucketRecordCorruptedException &ex) {
        m_io.cerr() << ex.message();
        return CYNARA_API_INVALID_COMMANDLINE_PARAM;
    } catch (const PolicyParsingException &) {
        m_io.cerr() << "Invalid policy" << std::endl;
        return CYNARA_API_INVALID_COMMANDLINE_PARAM;
    }
}

int CommandsDispatcher::execute(EraseCyadCommand &result) {
    const auto &key = result.policyKey();
    auto client = key.client().toString().c_str();
    auto user = key.user().toString().c_str();
    auto privilege = key.privilege().toString().c_str();

    auto ret = m_adminApiWrapper.cynara_admin_erase(m_cynaraAdmin, result.bucketId().c_str(),
                                                    result.recursive(), client, user, privilege);

    if (ret != CYNARA_API_SUCCESS)
        printAdminApiError(ret);

    return ret;
}

int CommandsDispatcher::execute(CheckCyadCommand &command) {
    const auto &key = command.policyKey();
    auto client = key.client().toString().c_str();
    auto user = key.user().toString().c_str();
    auto privilege = key.privilege().toString().c_str();

    // Initialization is needed to make compiler happy (-Werror=maybe-uninitialized, FTW)
    int result = CYNARA_API_ACCESS_DENIED;
    char *resultExtra = nullptr;

    auto ret =  m_adminApiWrapper.cynara_admin_check(m_cynaraAdmin, command.bucketId().c_str(),
                                                     command.recursive(), client, user, privilege,
                                                     &result, &resultExtra);

    if (ret == CYNARA_API_SUCCESS) {
        m_io.cout() << result << ";";

        if (resultExtra != nullptr) {
            m_io.cout() << resultExtra;
            free(resultExtra);
        }

        m_io.cout() << std::endl;
    } else {
        printAdminApiError(ret);
    }

    return ret;
}

int CommandsDispatcher::execute(ListPoliciesCyadCommand &command) {
    const auto &key = command.policyKey();
    auto client = key.client().toString().c_str();
    auto user = key.user().toString().c_str();
    auto privilege = key.privilege().toString().c_str();

    // Initialization is needed to make compiler happy (-Werror=maybe-uninitialized, FTW)
    cynara_admin_policy **policies = nullptr;

    auto ret = m_adminApiWrapper.cynara_admin_list_policies(m_cynaraAdmin,
                                                            command.bucketId().c_str(),
                                                            client, user, privilege,
                                                            &policies);

    auto printPolicy = [this] (cynara_admin_policy *p) {
        m_io.cout() << p->bucket << ";"
                    << p->client << ";"
                    << p->user << ";"
                    << p->privilege << ";"
                    << p->result << ";";
        if (p->result_extra != nullptr) {
            m_io.cout() << p->result_extra;
        }
        m_io.cout() << std::endl;
    };

    auto freePolicy = [] (cynara_admin_policy *p) {
        free(p->bucket);
        free(p->client);
        free(p->user);
        free(p->privilege);
        free(p->result_extra);
        free(p);
    };

    if (ret == CYNARA_API_SUCCESS) {
        for (int i = 0; policies[i]; ++i) {
            auto p = policies[i];
            printPolicy(p);
            freePolicy(p);
        }
        free(policies);
    } else {
        printAdminApiError(ret);
    }

    return ret;
}

int CommandsDispatcher::execute(ListPoliciesDescCyadCommand &) {

    for (const auto desc : m_policyTranslator.descriptions()) {
        m_io.cout() << desc.first << ";" << desc.second << std::endl;
    }

    return CYNARA_API_SUCCESS;
}

void CommandsDispatcher::printAdminApiError(int errnum) {
    const std::size_t buffSize = 256;
    char buf[buffSize];
    auto ret = m_errorApiWrapper.cynara_strerror(errnum, buf, buffSize);

    m_io.cerr() << "Cynara: [" << errnum << "] ";

    if (ret == CYNARA_API_SUCCESS)
        m_io.cerr() << buf << std::endl;
    else if (ret == CYNARA_API_INVALID_PARAM)
        m_io.cerr() << "Unknown error (sic!)" << std::endl;
    else if (ret == CYNARA_API_BUFFER_TOO_SHORT)
        m_io.cerr() << "Error message too long" << std::endl;
    else
        m_io.cerr() << "Unknown error (sic! sic!)" << std::endl;
}

void CommandsDispatcher::initPolicyTranslator(void) {
    // Initialization is needed to make compiler happy (-Werror=maybe-uninitialized, FTW)
    cynara_admin_policy_descr **descs = nullptr;

    auto ret = m_adminApiWrapper.cynara_admin_list_policies_descriptions(m_cynaraAdmin, &descs);

    auto freePolicyDesc = [] (cynara_admin_policy_descr *pd) {
        free(pd->name);
        free(pd);
    };

    if (ret == CYNARA_API_SUCCESS) {
        m_policyTranslator.setDescriptions(descs);
        for (int i = 0; descs[i] != nullptr; ++i) {
            freePolicyDesc(descs[i]);
        }
        free(descs);
    } else {
        throw ret;
    }
}

} /* namespace Cynara */
