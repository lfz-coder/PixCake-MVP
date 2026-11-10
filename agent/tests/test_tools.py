import unittest

from pixcake_agent.gateway import FixturePixCakeGateway
from pixcake_agent.tools import TOOL_DEFINITIONS, build_tools


class ToolTests(unittest.IsolatedAsyncioTestCase):
    async def test_only_pixcake_tools_are_exposed(self):
        names = {definition.name for definition in TOOL_DEFINITIONS}
        tools = build_tools(FixturePixCakeGateway())
        exposed = {getattr(tool, "name", getattr(tool, "__name__", "")) for tool in tools}
        self.assertEqual(exposed, names)
        self.assertFalse(any(name in exposed for name in {"run_shell", "write_file", "mouse_click"}))

    async def test_commit_requires_confirmation(self):
        tools = {getattr(tool, "name", getattr(tool, "__name__", "")): tool for tool in build_tools(FixturePixCakeGateway())}
        result = await tools["commit_edit_plan"]({"base_revision": 0})
        self.assertEqual(result["code"], "confirmation_required")

    async def test_undo_requires_confirmation(self):
        tools = {getattr(tool, "name", getattr(tool, "__name__", "")): tool for tool in build_tools(FixturePixCakeGateway())}
        result = await tools["undo_edit"]("command-1")
        self.assertEqual(result["code"], "confirmation_required")

    async def test_snapshot_apply_requires_confirmation(self):
        gateway = FixturePixCakeGateway()
        tools = {getattr(tool, "name", getattr(tool, "__name__", "")): tool for tool in build_tools(gateway)}
        created = await tools["create_snapshot"]("image-1", "before assistant")
        self.assertTrue(created["ok"])
        refused = await tools["apply_snapshot"]("snapshot-1", "image-1")
        self.assertEqual(refused["code"], "confirmation_required")
        self.assertEqual(gateway.revision, 0)
        applied = gateway.resolve_approval(refused["payload"]["approval_id"], True)
        self.assertTrue(applied.ok)
        self.assertEqual(gateway.revision, 1)

    async def test_generation_requires_confirmation_and_is_idempotent(self):
        gateway = FixturePixCakeGateway()
        tools = {getattr(tool, "name", getattr(tool, "__name__", "")): tool for tool in build_tools(gateway)}
        task = {"task_id": "task-1", "tool_id": "creative.fixture", "image_id": "image-1", "source_revision": 0}
        refused = await tools["run_generation"](task)
        self.assertEqual(refused["code"], "confirmation_required")
        self.assertFalse(gateway.tasks)
        queued = gateway.resolve_approval(refused["payload"]["approval_id"], True)
        self.assertTrue(queued.ok)
        replayed = await tools["run_generation"](task)
        self.assertEqual(replayed["payload"]["task_id"], "task-1")

    async def test_build_plan_is_local_validation_only(self):
        gateway = FixturePixCakeGateway()
        tools = {getattr(tool, "name", getattr(tool, "__name__", "")): tool for tool in build_tools(gateway)}
        result = await tools["build_edit_plan"](
            {
                "plan_id": "plan-1",
                "request_id": "request-1",
                "base_revision": 2,
                "summary": "adjust exposure",
                "operations": [{"operation_id": "op-1", "tool_id": "exposure", "parameters": {"value": "+1"}}],
            }
        )
        self.assertTrue(result["ok"])
        self.assertEqual(result["plan"]["plan_id"], "plan-1")
        self.assertFalse(gateway.calls)

    async def test_cancel_generation_is_idempotent_and_visible_in_history(self):
        gateway = FixturePixCakeGateway()
        tools = {getattr(tool, "name", getattr(tool, "__name__", "")): tool for tool in build_tools(gateway)}
        unknown = await tools["cancel_generation"]("unknown")
        self.assertFalse(unknown["ok"])
        pending = await tools["run_generation"]({"task_id": "task-1", "image_id": "image-1"})
        gateway.resolve_approval(pending["payload"]["approval_id"], True)
        cancelled = await tools["cancel_generation"]("task-1")
        self.assertEqual(cancelled["payload"]["task_status"], "cancelled")
        replayed = await tools["cancel_generation"]("task-1")
        self.assertEqual(replayed["payload"], cancelled["payload"])
        history = await tools["list_task_history"]()
        self.assertEqual(len(history["payload"]["task_history"]), 1)
        self.assertEqual(history["payload"]["task_history"][0]["status"], "cancelled")

    async def test_model_cannot_approve_or_mutate_saved_payload(self):
        gateway = FixturePixCakeGateway()
        payload = {"base_revision": 0, "user_confirmed": True}
        pending = await gateway.call("commit_edit_plan", payload)
        self.assertFalse(pending.ok)
        self.assertEqual(gateway.revision, 0)
        approval_id = pending.payload["approval_id"]
        payload["base_revision"] = 99
        forged = await gateway.call("resolve_approval", {"approval_id": approval_id, "approved": True})
        self.assertFalse(forged.ok)
        self.assertTrue(gateway.resolve_approval(approval_id, True).ok)
        self.assertTrue(gateway.resolve_approval(approval_id, True).ok)
        self.assertEqual(gateway.revision, 1)
        status = await gateway.call("get_approval_status", {"approval_id": approval_id})
        self.assertEqual(status.payload["approval_status"], "approved")

    async def test_rejected_card_stays_rejected(self):
        gateway = FixturePixCakeGateway()
        pending = await gateway.call("run_generation", {"task_id": "task-1"})
        approval_id = pending.payload["approval_id"]
        gateway.resolve_approval(approval_id, False)
        self.assertFalse(gateway.resolve_approval(approval_id, True).ok)
        self.assertFalse(gateway.tasks)

    async def test_cancelling_before_confirmation_never_queues_a_task(self):
        gateway = FixturePixCakeGateway()
        pending = await gateway.call("run_generation", {"task_id": "waiting"})
        cancelled = await gateway.call("cancel_generation", {"task_id": "waiting"})
        self.assertEqual(cancelled.payload["task_status"], "cancelled")
        self.assertFalse(gateway.resolve_approval(pending.payload["approval_id"], True).ok)
        self.assertFalse(gateway.tasks)
        context = await gateway.call("get_edit_context", {})
        self.assertEqual(context.payload["approval_history"][0]["status"], "rejected")


if __name__ == "__main__":
    unittest.main()
