#include "pc_clipboard_policy_file.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLatin1String>
#include <QSaveFile>
#include <QString>

namespace fusiondesk {
namespace apps {
namespace pc {

namespace {

void appendMessage(PcClipboardPolicyFileLoadResult& result,
                   const std::string& message)
{
    result.messages.push_back(message);
}

void appendMessage(PcClipboardPolicyFileSaveResult& result,
                   const std::string& message)
{
    result.messages.push_back(message);
}

bool readBool(const QJsonObject& object,
              const char* field,
              bool& target,
              PcClipboardPolicyFileLoadResult& result,
              const char* section)
{
    const QJsonValue value = object.value(QLatin1String(field));
    if (value.isUndefined())
        return true;
    if (!value.isBool()) {
        appendMessage(result,
                      std::string("clipboard policy file field ") +
                          section + "." + field + " must be boolean");
        return false;
    }

    target = value.toBool();
    return true;
}

bool readUnsigned(const QJsonObject& object,
                  const char* field,
                  std::uint64_t& target,
                  PcClipboardPolicyFileLoadResult& result,
                  const char* section)
{
    const QJsonValue value = object.value(QLatin1String(field));
    if (value.isUndefined())
        return true;
    if (!value.isDouble()) {
        appendMessage(result,
                      std::string("clipboard policy file field ") +
                          section + "." + field + " must be an integer");
        return false;
    }

    const double parsed = value.toDouble();
    if (!std::isfinite(parsed) || parsed < 0.0 ||
        parsed > static_cast<double>(std::numeric_limits<std::uint64_t>::max()) ||
        std::floor(parsed) != parsed) {
        appendMessage(result,
                      std::string("clipboard policy file field ") +
                          section + "." + field + " is out of range");
        return false;
    }

    target = static_cast<std::uint64_t>(parsed);
    return true;
}

bool readSizeT(const QJsonObject& object,
               const char* field,
               std::size_t& target,
               PcClipboardPolicyFileLoadResult& result,
               const char* section)
{
    std::uint64_t parsed = static_cast<std::uint64_t>(target);
    if (!readUnsigned(object, field, parsed, result, section))
        return false;
    if (parsed > static_cast<std::uint64_t>(
                     std::numeric_limits<std::size_t>::max())) {
        appendMessage(result,
                      std::string("clipboard policy file field ") +
                          section + "." + field + " is out of range");
        return false;
    }

    target = static_cast<std::size_t>(parsed);
    return true;
}

bool readString(const QJsonObject& object,
                const char* field,
                std::string& target,
                PcClipboardPolicyFileLoadResult& result,
                const char* section)
{
    const QJsonValue value = object.value(QLatin1String(field));
    if (value.isUndefined())
        return true;
    if (!value.isString()) {
        appendMessage(result,
                      std::string("clipboard policy file field ") +
                          section + "." + field + " must be a string");
        return false;
    }

    target = value.toString().toStdString();
    return true;
}

bool readPolicyObjectFromRoot(const QJsonObject& root,
                              QJsonObject& policyObject,
                              PcClipboardPolicyFileLoadResult& result)
{
    const QJsonValue nested = root.value(QLatin1String("clipboardPolicy"));
    if (nested.isUndefined()) {
        policyObject = root;
        return true;
    }
    if (!nested.isObject()) {
        appendMessage(result,
                      "clipboard policy file field clipboardPolicy must be an object");
        return false;
    }

    policyObject = nested.toObject();
    return true;
}

bool readChildObject(const QJsonObject& object,
                     const char* primary,
                     const char* fallback,
                     QJsonObject& child,
                     PcClipboardPolicyFileLoadResult& result)
{
    const QJsonValue primaryValue = object.value(QLatin1String(primary));
    if (!primaryValue.isUndefined()) {
        if (!primaryValue.isObject()) {
            appendMessage(result,
                          std::string("clipboard policy file field ") +
                              primary + " must be an object");
            return false;
        }
        child = primaryValue.toObject();
        return true;
    }

    const QJsonValue fallbackValue = object.value(QLatin1String(fallback));
    if (!fallbackValue.isUndefined()) {
        if (!fallbackValue.isObject()) {
            appendMessage(result,
                          std::string("clipboard policy file field ") +
                              fallback + " must be an object");
            return false;
        }
        child = fallbackValue.toObject();
        return true;
    }

    child = {};
    return true;
}

bool readModulePolicy(const QJsonObject& object,
                      runtime::ProductClipboardPolicy& policy,
                      PcClipboardPolicyFileLoadResult& result)
{
    if (object.isEmpty())
        return true;

    modules::clipboard::ClipboardPolicy& module = policy.modulePolicy;
    bool ok = true;
    ok = readBool(object, "allowAnnounce", module.allowAnnounce, result, "module") && ok;
    ok = readBool(object, "allowReceive", module.allowReceive, result, "module") && ok;
    ok = readBool(object, "allowSendContent", module.allowSendContent, result, "module") && ok;
    ok = readBool(object, "allowWriteLocal", module.allowWriteLocal, result, "module") && ok;
    ok = readBool(object, "allowPresentationMetadata", module.allowPresentationMetadata, result, "module") && ok;
    ok = readBool(object, "allowPlainText", module.allowPlainText, result, "module") && ok;
    ok = readBool(object, "allowHtml", module.allowHtml, result, "module") && ok;
    ok = readBool(object, "allowRtf", module.allowRtf, result, "module") && ok;
    ok = readBool(object, "allowImage", module.allowImage, result, "module") && ok;
    ok = readBool(object, "allowFileList", module.allowFileList, result, "module") && ok;
    ok = readBool(object, "allowFileContents", module.allowFileContents, result, "module") && ok;
    ok = readBool(object, "allowDrag", module.allowDrag, result, "module") && ok;
    ok = readBool(object, "allowCustomFormats", module.allowCustomFormats, result, "module") && ok;
    ok = readUnsigned(object, "maxInlineBytes", module.maxInlineBytes, result, "module") && ok;
    ok = readUnsigned(object, "maxFileRangeBytes", module.maxFileRangeBytes, result, "module") && ok;
    ok = readUnsigned(object, "maxFileCount", module.maxFileCount, result, "module") && ok;
    ok = readUnsigned(object, "maxSingleFileBytes", module.maxSingleFileBytes, result, "module") && ok;
    return ok;
}

bool readRuntimePolicy(const QJsonObject& object,
                       runtime::ProductClipboardPolicy& policy,
                       PcClipboardPolicyFileLoadResult& result)
{
    if (object.isEmpty())
        return true;

    runtime::feature::ClipboardRuntimePolicyRules& runtime =
        policy.runtimeRules;
    bool ok = true;
    ok = readBool(object, "auditAllowed", runtime.auditAllowed, result, "runtime") && ok;
    ok = readSizeT(object, "maxRecentAuditEvents", runtime.maxRecentAuditEvents, result, "runtime") && ok;
    ok = readBool(object, "allowLocalSnapshotAnnounce", runtime.allowLocalSnapshotAnnounce, result, "runtime") && ok;
    ok = readBool(object, "allowRemoteFormatRead", runtime.allowRemoteFormatRead, result, "runtime") && ok;
    ok = readBool(object, "allowRemoteFileRangeRead", runtime.allowRemoteFileRangeRead, result, "runtime") && ok;
    ok = readBool(object, "allowRemoteObjectLock", runtime.allowRemoteObjectLock, result, "runtime") && ok;
    ok = readBool(object, "allowRemoteObjectUnlock", runtime.allowRemoteObjectUnlock, result, "runtime") && ok;
    ok = readBool(object, "allowPendingReadExpiry", runtime.allowPendingReadExpiry, result, "runtime") && ok;
    ok = readString(object, "denialReason", runtime.denialReason, result, "runtime") && ok;
    return ok;
}

void writeBool(QJsonObject& object, const char* field, bool value)
{
    object.insert(QLatin1String(field), value);
}

void writeUnsigned(QJsonObject& object,
                   const char* field,
                   std::uint64_t value)
{
    object.insert(QLatin1String(field),
                  QJsonValue(static_cast<double>(value)));
}

void writeSizeT(QJsonObject& object, const char* field, std::size_t value)
{
    object.insert(QLatin1String(field),
                  QJsonValue(static_cast<double>(value)));
}

void writeString(QJsonObject& object,
                 const char* field,
                 const std::string& value)
{
    object.insert(QLatin1String(field), QString::fromStdString(value));
}

QJsonObject modulePolicyToJson(
    const modules::clipboard::ClipboardPolicy& module)
{
    QJsonObject object;
    writeBool(object, "allowAnnounce", module.allowAnnounce);
    writeBool(object, "allowReceive", module.allowReceive);
    writeBool(object, "allowSendContent", module.allowSendContent);
    writeBool(object, "allowWriteLocal", module.allowWriteLocal);
    writeBool(object,
              "allowPresentationMetadata",
              module.allowPresentationMetadata);
    writeBool(object, "allowPlainText", module.allowPlainText);
    writeBool(object, "allowHtml", module.allowHtml);
    writeBool(object, "allowRtf", module.allowRtf);
    writeBool(object, "allowImage", module.allowImage);
    writeBool(object, "allowFileList", module.allowFileList);
    writeBool(object, "allowFileContents", module.allowFileContents);
    writeBool(object, "allowDrag", module.allowDrag);
    writeBool(object, "allowCustomFormats", module.allowCustomFormats);
    writeUnsigned(object, "maxInlineBytes", module.maxInlineBytes);
    writeUnsigned(object, "maxFileRangeBytes", module.maxFileRangeBytes);
    writeUnsigned(object, "maxFileCount", module.maxFileCount);
    writeUnsigned(object, "maxSingleFileBytes", module.maxSingleFileBytes);
    return object;
}

QJsonObject runtimePolicyToJson(
    const runtime::feature::ClipboardRuntimePolicyRules& runtime)
{
    QJsonObject object;
    writeBool(object, "auditAllowed", runtime.auditAllowed);
    writeSizeT(object,
               "maxRecentAuditEvents",
               runtime.maxRecentAuditEvents);
    writeBool(object,
              "allowLocalSnapshotAnnounce",
              runtime.allowLocalSnapshotAnnounce);
    writeBool(object, "allowRemoteFormatRead", runtime.allowRemoteFormatRead);
    writeBool(object,
              "allowRemoteFileRangeRead",
              runtime.allowRemoteFileRangeRead);
    writeBool(object, "allowRemoteObjectLock", runtime.allowRemoteObjectLock);
    writeBool(object,
              "allowRemoteObjectUnlock",
              runtime.allowRemoteObjectUnlock);
    writeBool(object,
              "allowPendingReadExpiry",
              runtime.allowPendingReadExpiry);
    writeString(object, "denialReason", runtime.denialReason);
    return object;
}

void normalizeProductPolicy(runtime::ProductClipboardPolicy& policy)
{
    if (!policy.modulePolicy.allowFileContents) {
        policy.runtimeRules.allowRemoteFileRangeRead = false;
        policy.runtimeRules.allowRemoteObjectLock = false;
        policy.runtimeRules.allowRemoteObjectUnlock = false;
    }
}

} // namespace

PcClipboardPolicyFileLoadResult loadClipboardProductPolicyFromJsonFile(
    const std::string& path,
    runtime::ProductClipboardPolicy basePolicy)
{
    PcClipboardPolicyFileLoadResult result;
    result.policy = basePolicy;
    if (path.empty()) {
        appendMessage(result, "clipboard policy file path is empty");
        return result;
    }

    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) {
        appendMessage(result, "clipboard policy file cannot be opened: " + path);
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(),
                                                           &parseError);
    if (parseError.error != QJsonParseError::NoError ||
        !document.isObject()) {
        appendMessage(result, "clipboard policy file is not valid json: " + path);
        return result;
    }

    QJsonObject policyObject;
    if (!readPolicyObjectFromRoot(document.object(), policyObject, result))
        return result;

    QJsonObject moduleObject;
    if (!readChildObject(policyObject,
                         "module",
                         "modulePolicy",
                         moduleObject,
                         result)) {
        return result;
    }
    QJsonObject runtimeObject;
    if (!readChildObject(policyObject,
                         "runtime",
                         "runtimeRules",
                         runtimeObject,
                         result)) {
        return result;
    }

    bool ok = true;
    ok = readModulePolicy(moduleObject, result.policy, result) && ok;
    ok = readRuntimePolicy(runtimeObject, result.policy, result) && ok;
    if (!ok)
        return result;

    normalizeProductPolicy(result.policy);
    result.ok = true;
    result.loaded = true;
    return result;
}

std::string clipboardProductPolicyToJson(
    runtime::ProductClipboardPolicy policy,
    bool compact)
{
    normalizeProductPolicy(policy);

    QJsonObject policyObject;
    policyObject.insert(QLatin1String("module"),
                        modulePolicyToJson(policy.modulePolicy));
    policyObject.insert(QLatin1String("runtime"),
                        runtimePolicyToJson(policy.runtimeRules));

    QJsonObject root;
    root.insert(QLatin1String("clipboardPolicy"), policyObject);

    const QJsonDocument document(root);
    const QByteArray bytes = document.toJson(
        compact ? QJsonDocument::Compact : QJsonDocument::Indented);
    return std::string(bytes.constData(),
                       static_cast<std::size_t>(bytes.size()));
}

PcClipboardPolicyFileSaveResult saveClipboardProductPolicyToJsonFile(
    const std::string& path,
    runtime::ProductClipboardPolicy policy)
{
    PcClipboardPolicyFileSaveResult result;
    if (path.empty()) {
        appendMessage(result, "clipboard policy save path is empty");
        return result;
    }

    const std::string json = clipboardProductPolicyToJson(std::move(policy));
    QSaveFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::WriteOnly)) {
        appendMessage(result, "clipboard policy file cannot be opened for write: " + path);
        return result;
    }

    const QByteArray bytes(json.data(),
                           static_cast<qsizetype>(json.size()));
    if (file.write(bytes) != bytes.size()) {
        appendMessage(result, "clipboard policy file write failed: " + path);
        return result;
    }
    if (!file.commit()) {
        appendMessage(result, "clipboard policy file commit failed: " + path);
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace pc
} // namespace apps
} // namespace fusiondesk
