/***************************************************************************
 * rs_operation_logger.cpp  —  Lab experiment operation logger
 ***************************************************************************/
#include "rs_operation_logger.h"

#include <QFile>
#include <QTextStream>
#include <QDateTime>

#include <sstream>
#include <iomanip>

namespace sicnu::operators {

RSOperationLogger& RSOperationLogger::instance() {
    static RSOperationLogger s_instance;
    return s_instance;
}

size_t RSOperationLogger::beginRun(const std::string& operatorName, const Json::Value& params) {
    std::lock_guard<std::mutex> lock(m_mutex);

    OperationRecord record;
    record.operatorName = operatorName;
    record.parameters = params;
    record.startTimeIso = nowIso8601();

    m_records.push_back(record);
    return m_records.size() - 1;
}

void RSOperationLogger::finishRun(size_t handle, const Json::Value& result, double durationMs) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (handle >= m_records.size())
        return;

    auto& record = m_records[handle];
    record.result = result;
    record.success = true;
    record.endTimeIso = nowIso8601();
    record.durationMs = durationMs;
}

void RSOperationLogger::failRun(size_t handle, int errorCode, const std::string& errorMessage, double durationMs) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (handle >= m_records.size())
        return;

    auto& record = m_records[handle];
    record.success = false;
    record.errorCode = errorCode;
    record.errorMessage = errorMessage;
    record.endTimeIso = nowIso8601();
    record.durationMs = durationMs;
}

Json::Value RSOperationLogger::toJson() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    Json::Value root(Json::arrayValue);
    for (const auto& r : m_records) {
        Json::Value entry(Json::objectValue);
        entry["operator"] = r.operatorName;
        entry["parameters"] = r.parameters;
        entry["result"] = r.result;
        entry["success"] = r.success;
        entry["errorCode"] = r.errorCode;
        entry["errorMessage"] = r.errorMessage;
        entry["startTime"] = r.startTimeIso;
        entry["endTime"] = r.endTimeIso;
        entry["durationMs"] = r.durationMs;
        root.append(entry);
    }
    return root;
}

std::string RSOperationLogger::toCsv() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::ostringstream oss;
    oss << "operator,success,errorCode,errorMessage,startTime,endTime,durationMs,paramsJson\n";

    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "";
    const auto writer = std::unique_ptr<Json::StreamWriter>(writerBuilder.newStreamWriter());

    for (const auto& r : m_records) {
        std::string paramsStr;
        {
            std::ostringstream paramsOss;
            writer->write(r.parameters, &paramsOss);
            paramsStr = paramsOss.str();
        }

        // CSV escaping: wrap fields containing comma/quote in quotes
        auto escape = [](const std::string& s) {
            if (s.find_first_of(",\"\n") == std::string::npos)
                return s;
            std::string out = "\"";
            for (char c : s) {
                if (c == '"') out += "\"\"";
                else out += c;
            }
            out += "\"";
            return out;
        };

        oss << escape(r.operatorName) << ','
            << (r.success ? "true" : "false") << ','
            << r.errorCode << ','
            << escape(r.errorMessage) << ','
            << escape(r.startTimeIso) << ','
            << escape(r.endTimeIso) << ','
            << r.durationMs << ','
            << escape(paramsStr) << '\n';
    }
    return oss.str();
}

bool RSOperationLogger::exportToFile(const std::string& filePath, std::string* errorMessage) const {
    const QString qPath = QString::fromStdString(filePath);
    QFile file(qPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) *errorMessage = "Cannot open file for writing: " + filePath;
        return false;
    }

    QTextStream out(&file);
    if (qPath.endsWith(".csv", Qt::CaseInsensitive)) {
        out << QString::fromStdString(toCsv());
    } else {
        // Default to JSON
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "  ";
        const std::string jsonStr = Json::writeString(builder, toJson());
        out << QString::fromStdString(jsonStr);
    }

    if (out.status() != QTextStream::Ok) {
        if (errorMessage) *errorMessage = "Failed to write to file: " + filePath;
        return false;
    }
    return true;
}

void RSOperationLogger::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_records.clear();
}

size_t RSOperationLogger::recordCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_records.size();
}

std::vector<OperationRecord> RSOperationLogger::records() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_records;
}

std::string RSOperationLogger::nowIso8601() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
}

} // namespace sicnu::operators
