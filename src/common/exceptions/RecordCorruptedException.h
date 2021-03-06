/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd All Rights Reserved
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
 * @file        src/common/exceptions/RecordCorruptedException.h
 * @author      Pawel Wieczorek <p.wieczorek2@samsung.com>
 * @version     1.0
 * @brief       Implementation of RecordCorruptedException
 */

#ifndef SRC_COMMON_EXCEPTIONS_RECORDCORRUPTEDEXCEPTION_H_
#define SRC_COMMON_EXCEPTIONS_RECORDCORRUPTEDEXCEPTION_H_

#include <string>

#include <exceptions/DatabaseException.h>

namespace Cynara {

class RecordCorruptedException : public DatabaseException {
public:
    RecordCorruptedException(void) = delete;
    virtual ~RecordCorruptedException() {};

    RecordCorruptedException(const std::string &line)
        : m_lineNumber(0), m_line(line), m_filename(std::string()) {
        setMessage(std::string());
    }

    const std::string &message(void) const {
        return m_message;
    }

    const std::string &filename(void) const {
        return m_filename;
    }

    const std::string &line(void) const {
        return m_line;
    }

    size_t lineNumber(void) const {
        return m_lineNumber;
    }

protected:
    std::string slicedLine(void) const {
        return m_line.substr(0, 50) + (m_line.size() > 50 ? "..." : "");
    }

    std::string formattedMetadata(void) const {
        return (m_filename.empty() ? "line" : m_filename)
                + (m_lineNumber <= 0 ? "" : formattedLineNumber());
    }

    std::string formattedLineNumber(void) const {
        return (m_filename.empty() ? " " : ":")
                + std::to_string(static_cast<long int>(m_lineNumber));
    }

    void setMessage(const std::string &subtype) {
        m_message = (subtype.empty() ? "Record" : subtype + " record") + " corrupted at "
                    + formattedMetadata() + ": <" + slicedLine() + ">";
    }

    size_t m_lineNumber;
    std::string m_line;
    std::string m_filename;
    std::string m_message;
};

} /* namespace Cynara */

#endif /* SRC_COMMON_EXCEPTIONS_RECORDCORRUPTEDEXCEPTION_H_ */
