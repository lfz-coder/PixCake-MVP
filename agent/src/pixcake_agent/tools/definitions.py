"""Single source of truth for PixCake tool metadata and risk levels."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Literal

Risk = Literal["auto", "confirm", "blocked"]


@dataclass(frozen=True)
class ToolDefinition:
    name: str
    category: str
    risk: Risk
    description: str
    requires_preview: bool = False


TOOL_DEFINITIONS: tuple[ToolDefinition, ...] = (
    ToolDefinition("get_approval_status", "interaction", "auto", "只读查询桌面确认结果，不能批准操作。"),
    ToolDefinition("get_edit_context", "context", "auto", "读取当前图片、编辑实例、修订号和可用 PixCake 能力。"),
    ToolDefinition("get_selection_context", "context", "auto", "读取当前选区、人物或语义目标的引用信息。"),
    ToolDefinition("create_task_plan", "planning", "auto", "创建可展示的多步任务清单，不执行修改。"),
    ToolDefinition("build_edit_plan", "planning", "auto", "把结构化操作整理成版本化编辑计划，不执行修改。"),
    ToolDefinition("preview_edit_plan", "editing", "auto", "请求 C++ 生成非持久化预览。", True),
    ToolDefinition("request_structured_card", "interaction", "auto", "请求 UI 展示导入、挑图、导出或点数确认卡片。"),
    ToolDefinition("commit_edit_plan", "editing", "confirm", "请求桌面确认已预览的编辑计划，待确认不表示已提交。", True),
    ToolDefinition("reject_edit_plan", "editing", "auto", "拒绝当前计划并保留原编辑文档。"),
    ToolDefinition("undo_edit", "editing", "confirm", "撤销一项已经提交的助手编辑。"),
    ToolDefinition("run_generation", "generation", "confirm", "创建需要点数确认的生成任务。", True),
    ToolDefinition("cancel_generation", "generation", "auto", "请求取消生成任务；最终状态和扣点由 C++ 账本决定。"),
    ToolDefinition("create_snapshot", "history", "auto", "保存当前助手效果快照。"),
    ToolDefinition("apply_snapshot", "history", "confirm", "将已有快照应用回当前照片参数。", True),
    ToolDefinition("list_task_history", "history", "auto", "读取本地生成任务历史，不启动新任务。"),
)


TOOL_BY_NAME = {item.name: item for item in TOOL_DEFINITIONS}
