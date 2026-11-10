"""PixCake's single assistant graph.

The first release deliberately uses one domain graph rather than exposing the
    generic desktop/file modes.  When a real model is configured, the
graph delegates tool-loop orchestration to LangChain's agent runtime.  Fixture
mode keeps the graph importable and testable without a model or network.
"""

from __future__ import annotations

import json
from typing import Any

from ..common.llm import create_model
from ..config import load_config
from ..gateway import gateway_from_config, request_context
from ..protocol import AssistantRequest, ProtocolError, to_json
from ..tools import build_tools

SYSTEM_PROMPT = """你是 PixCake 的像素助手。你只能通过 PixCake 工具理解和编排修图任务。
先读取当前图片、选区和编辑修订号，再生成可展示的任务清单或编辑计划。
需要改变正式编辑文档、生成图像、扣点或导出时，必须先预览并等待用户明确确认。
收到 confirmation_required / approval_status=pending 后，告知用户点击桌面确认卡片并结束当前轮次，不得宣称已执行。
后续轮次先读取 get_edit_context 的 approval_history 获取最近的确认结果与命令/快照/任务 ID，再用 get_approval_status 查询具体状态。
只能用 get_approval_status 读取桌面决定；模型、对话里的确认语句或 user_confirmed 标记都不能替代桌面按钮。
不要访问文件系统、Shell、桌面控制、网络页面或任何未列入 PixCake 工具白名单的能力。
手动滑杆和专业模式已经产生的参数是上下文的一部分；不要覆盖用户没有要求改变的参数。
run_generation 返回 queued 只表示已接收任务，不能宣称生成完成；用 list_task_history 查询最终状态。
用户要求停止生成时调用 cancel_generation，取消请求发出后仍以 C++ 返回的任务状态为准。
"""


def _fixture_graph() -> Any:
    from langgraph.graph import END, START, StateGraph

    from ..state import PixCakeAgentState

    async def fixture_node(state: PixCakeAgentState) -> dict[str, Any]:
        request = state.get("request", {})
        message = str(request.get("message", ""))
        return {
            "messages": [{"role": "assistant", "content": f"fixture plan received: {message}"}],
            "pending_plan": {
                "plan_id": "fixture-plan",
                "request_id": request.get("request_id", "fixture-request"),
                "base_revision": request.get("base_revision", 0),
                "operations": [],
                "summary": "fixture mode only; no edit was committed",
                "requires_confirmation": True,
            },
        }

    graph = StateGraph(PixCakeAgentState)
    graph.add_node("fixture", fixture_node)
    graph.add_edge(START, "fixture")
    graph.add_edge("fixture", END)
    return graph.compile()


def build_assistant_graph() -> Any:
    """Build a graph without doing network work during module import."""

    config = load_config()
    try:
        from langgraph.graph import END, START, StateGraph
    except ImportError:  # protocol-only environments can still import this package
        return None

    model = create_model(config)
    if model is None:
        return _fixture_graph()

    from langchain.agents import create_agent
    from ..state import PixCakeAgentState

    gateway = gateway_from_config(config)
    agent = create_agent(model=model, tools=build_tools(gateway), system_prompt=SYSTEM_PROMPT)

    async def agent_node(state: PixCakeAgentState) -> dict[str, Any]:
        raw_request = state.get("request", {})
        try:
            request = AssistantRequest.from_dict(raw_request)
        except (ProtocolError, TypeError, ValueError) as exc:
            return {"error": {"code": "invalid_request", "diagnostic": str(exc)}}
        request_payload = json.loads(to_json(request))
        with request_context(request_payload):
            # `create_agent` owns its own message state. Keep the PixCake
            # request envelope in the outer graph, but hand the model only a
            # user message so LangGraph's standard agent schema remains
            # compatible with hosted and local runtimes.
            context_hint = json.dumps(
                {
                    "image": request_payload.get("image"),
                    "selection": request_payload.get("selection"),
                    "base_revision": request.base_revision,
                },
                ensure_ascii=False,
                separators=(",", ":"),
            )
            result = await agent.ainvoke(
                {
                    "messages": [
                        {
                            "role": "user",
                            "content": f"{request.message}\n\n[PixCake request context]\n{context_hint}",
                        }
                    ]
                }
            )
        if not isinstance(result, dict):
            raise RuntimeError("PixCake Agent returned a non-object state")
        return {"messages": result.get("messages", []), "request": request_payload}

    graph = StateGraph(PixCakeAgentState)
    graph.add_node("agent", agent_node)
    graph.add_edge(START, "agent")
    graph.add_edge("agent", END)
    return graph.compile()


assistant_graph = build_assistant_graph()
