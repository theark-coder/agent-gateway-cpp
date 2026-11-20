
import json
import sys


def respond(request):
    prompt = request.get("prompt", "")
    step = request.get("step", 0)

    if not isinstance(prompt, str):
        return {"type": "error", "message": "invalid_prompt"}

    if not isinstance(step, int) or step < 0:
        return {"type": "error", "message": "invalid_step"}

    # 模拟模型要求调用一个工具
    if prompt.startswith("policy:"):
        topic = prompt.split(":", 1)[1].strip()

        if step == 0:
            return {
                "type": "tool_call",
                "name": "lookup_policy",
                "arguments": {"input": topic},
            }

        tool_result = request.get("tool_result")

        if not isinstance(tool_result, str):
            return {
                "type": "error",
                "message": "missing_tool_result",
            }

        return {
            "type": "final",
            "content": tool_result,
        }

    return {
        "type": "final",
        "content": f"FAKE: {prompt}",
    }


for line in sys.stdin:
    try:
        request = json.loads(line)
        if not isinstance(request, dict):
            raise ValueError("request must be an object")

        response = respond(request)
    except (json.JSONDecodeError, ValueError) as exc:
        response = {
            "type": "error",
            "message": str(exc),
        }


    print(json.dumps(response, ensure_ascii=False), flush=True)
