#include "archive_format.h"

#include "json_localization.h"

namespace z7::ui::archive_support {

QString stage_label_for(z7::app::OperationStage stage) {
  switch (stage) {
    case z7::app::OperationStage::kPrepare:
      return z7::i18n::text(
          QStringLiteral("ui.state.operation_stage.prepare"));
    case z7::app::OperationStage::kRunning:
      return z7::i18n::text(
          QStringLiteral("ui.state.operation_stage.running"));
    case z7::app::OperationStage::kFinalizing:
      return z7::i18n::text(
          QStringLiteral("ui.state.operation_stage.finalizing"));
    case z7::app::OperationStage::kCompleted:
      return z7::i18n::text(
          QStringLiteral("ui.state.operation_stage.completed"));
  }
  return z7::i18n::text(
      QStringLiteral("ui.state.operation_stage.running"));
}

QString format_binary_size(uint64_t bytes) {
  static constexpr uint64_t kKiB = 1ULL << 10;
  static constexpr uint64_t kMiB = 1ULL << 20;
  static constexpr uint64_t kGiB = 1ULL << 30;
  static constexpr uint64_t kGiBThreshold = 10ULL << 30;
  static constexpr uint64_t kMiBThreshold = 10ULL << 20;

  if (bytes >= kGiBThreshold) {
    return QStringLiteral("%1 GiB").arg(bytes / kGiB);
  }
  if (bytes >= kMiBThreshold) {
    return QStringLiteral("%1 MiB").arg(bytes / kMiB);
  }
  if (bytes >= kKiB) {
    return QStringLiteral("%1 KiB").arg(bytes / kKiB);
  }
  return QStringLiteral("%1 B").arg(bytes);
}

QString format_dual_size(uint64_t bytes) {
  return QStringLiteral("%1 bytes : %2")
      .arg(QString::number(bytes), format_binary_size(bytes));
}

}  // namespace z7::ui::archive_support
