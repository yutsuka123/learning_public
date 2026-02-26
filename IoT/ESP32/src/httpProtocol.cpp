/**
 * @file httpProtocol.cpp
 * @brief HTTPS APIプロトコル定義の実装。
 */

#include "httpProtocol.h"

namespace {

String escapeJsonString(const String& value) {
  String escapedValue;
  escapedValue.reserve(value.length() + 8);
  for (uint32_t index = 0; index < value.length(); ++index) {
    const char currentChar = value[index];
    if (currentChar == '\\' || currentChar == '"') {
      escapedValue += '\\';
      escapedValue += currentChar;
      continue;
    }
    if (currentChar == '\n') {
      escapedValue += "\\n";
      continue;
    }
    if (currentChar == '\r') {
      escapedValue += "\\r";
      continue;
    }
    if (currentChar == '\t') {
      escapedValue += "\\t";
      continue;
    }
    escapedValue += currentChar;
  }
  return escapedValue;
}

bool extractJsonStringValue(const String& payload, const String& key, String* outValue) {
  if (outValue == nullptr) {
    return false;
  }

  const String keyPattern = "\"" + key + "\":\"";
  const int32_t keyIndex = payload.indexOf(keyPattern);
  if (keyIndex < 0) {
    return false;
  }

  const int32_t valueStartIndex = keyIndex + static_cast<int32_t>(keyPattern.length());
  const int32_t valueEndIndex = payload.indexOf("\"", valueStartIndex);
  if (valueEndIndex < 0 || valueEndIndex < valueStartIndex) {
    return false;
  }

  *outValue = payload.substring(valueStartIndex, valueEndIndex);
  return true;
}

}  // namespace

namespace httpProtocol {

String buildPathHealth() {
  return "/api/v1/health";
}

String buildPathOtaPrepare(const String& publicId) {
  return "/api/v1/device/" + publicId + "/ota/prepare";
}

String buildPathOtaStart(const String& publicId) {
  return "/api/v1/device/" + publicId + "/ota/start";
}

String buildPathOtaProgress(const String& publicId) {
  return "/api/v1/device/" + publicId + "/ota/progress";
}

String buildOtaStartRequestPayload(const otaStartRequestModel& model) {
  String payload;
  payload.reserve(384);
  payload += "{";
  payload += "\"protocolVersion\":\"";
  payload += iotCommon::kProtocolVersion;
  payload += "\",";
  payload += "\"command\":\"";
  payload += toCommandName(httpApiCommandType::kOtaStart);
  payload += "\",";
  payload += "\"transactionId\":\"";
  payload += escapeJsonString(model.transactionId);
  payload += "\",";
  payload += "\"firmwareVersion\":\"";
  payload += escapeJsonString(model.firmwareVersion);
  payload += "\",";
  payload += "\"firmwareUrl\":\"";
  payload += escapeJsonString(model.firmwareUrl);
  payload += "\",";
  payload += "\"firmwareSha256\":\"";
  payload += escapeJsonString(model.firmwareSha256);
  payload += "\"";
  payload += "}";
  return payload;
}

bool parseOtaStartRequestPayload(const String& payload, otaStartRequestModel* outModel) {
  if (outModel == nullptr) {
    return false;
  }

  otaStartRequestModel parsedModel;
  if (!extractJsonStringValue(payload, "transactionId", &parsedModel.transactionId)) {
    return false;
  }
  if (!extractJsonStringValue(payload, "firmwareVersion", &parsedModel.firmwareVersion)) {
    return false;
  }
  if (!extractJsonStringValue(payload, "firmwareUrl", &parsedModel.firmwareUrl)) {
    return false;
  }
  if (!extractJsonStringValue(payload, "firmwareSha256", &parsedModel.firmwareSha256)) {
    return false;
  }

  *outModel = parsedModel;
  return true;
}

String buildStandardResponsePayload(
    bool isSuccess, httpApiCommandType command, const String& message) {
  String payload;
  payload.reserve(256);
  payload += "{";
  payload += "\"protocolVersion\":\"";
  payload += iotCommon::kProtocolVersion;
  payload += "\",";
  payload += "\"command\":\"";
  payload += toCommandName(command);
  payload += "\",";
  payload += "\"result\":\"";
  payload += isSuccess ? "success" : "error";
  payload += "\",";
  payload += "\"message\":\"";
  payload += escapeJsonString(message);
  payload += "\"";
  payload += "}";
  return payload;
}

const char* toCommandName(httpApiCommandType command) {
  switch (command) {
    case httpApiCommandType::kHealth:
      return "health";
    case httpApiCommandType::kOtaPrepare:
      return "otaPrepare";
    case httpApiCommandType::kOtaStart:
      return "otaStart";
    case httpApiCommandType::kOtaProgress:
      return "otaProgress";
    default:
      return "unknown";
  }
}

}  // namespace httpProtocol
