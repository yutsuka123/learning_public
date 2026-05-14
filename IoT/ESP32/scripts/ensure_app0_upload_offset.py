"""
PlatformIO upload offset guard.

[重要] 現行 ESP32-S3 パーティション表では app0 は 0x30000 である。
[厳守] PlatformIO 標準 upload が旧 factory 相当の 0x10000 に戻ることを禁止する。
[理由] 0x10000 へ app を書くと、正規 app0=0x30000 / app1=0x430000 構成と不整合になり、
       旧面起動や再起動ループ、試験結果の誤判定につながるため。

制限事項:
- 本スクリプトは PlatformIO / SCons の pre extra_script として実行する。
- 実機への書込みは行わず、設定値の検証だけを行う。
"""

from SCons.Script import ARGUMENTS, DefaultEnvironment


EXPECTED_APP0_UPLOAD_OFFSET = "0x30000"
LEGACY_FACTORY_UPLOAD_OFFSET = "0x10000"


def normalize_offset_text(offset_value):
    """
    @brief PlatformIO設定値を比較用の16進文字列へ正規化する。
    @param offset_value PlatformIO board_config から取得した offset 値。
    @return str 小文字の16進文字列。未設定の場合は空文字。
    """
    if offset_value is None:
        return ""
    if isinstance(offset_value, int):
        return hex(offset_value).lower()
    offset_text = str(offset_value).strip().lower()
    if offset_text.startswith("0x"):
        try:
            return hex(int(offset_text, 16)).lower()
        except ValueError:
            return offset_text
    if offset_text.isdigit():
        return hex(int(offset_text, 10)).lower()
    return offset_text


def is_upload_target_requested():
    """
    @brief 現在の PlatformIO 実行が upload を含むか判定する。
    @return bool upload target が要求されている場合 true。
    """
    targets = [str(target_name).lower() for target_name in ARGUMENTS.get("PIOENV_TARGETS", "").split(",")]
    if "upload" in targets:
        return True
    build_targets = ARGUMENTS.get("BUILD_TARGETS", "")
    return "upload" in str(build_targets).lower()


env = DefaultEnvironment()
board_config = env.BoardConfig()
actual_offset = normalize_offset_text(board_config.get("upload.offset_address"))

if actual_offset != EXPECTED_APP0_UPLOAD_OFFSET:
    raise RuntimeError(
        "ensure_app0_upload_offset failed. "
        f"board_upload.offset_address must be {EXPECTED_APP0_UPLOAD_OFFSET}, actual={actual_offset or '(unset)'}. "
        f"[禁止] legacy factory offset {LEGACY_FACTORY_UPLOAD_OFFSET} must not be used while app0 is 0x30000."
    )

if is_upload_target_requested():
    print(
        "[IMPORTANT] PlatformIO upload offset guard passed: "
        f"firmware upload offset={actual_offset}, expected app0={EXPECTED_APP0_UPLOAD_OFFSET}."
    )
