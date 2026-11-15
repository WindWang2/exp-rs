// Deterministic local Chat Completions fixture; no model credentials or cloud calls.
import { createServer } from "node:http";

export async function openAiFixture(handler) {
  const requests = [];
  const connections = new Set();
  const server = createServer(async (request, response) => {
    try {
      const buffers = [];
      let bytes = 0;
      for await (const chunk of request) {
        bytes += chunk.length;
        if (bytes > 2 * 1024 * 1024) throw new Error("Fixture request too large");
        buffers.push(chunk);
      }
      if (request.method !== "POST" || request.url !== "/v1/chat/completions") {
        response.writeHead(404).end();
        return;
      }
      const body = JSON.parse(Buffer.concat(buffers).toString("utf8"));
      requests.push({ body, headers: request.headers });
      const selected = await handler(body, requests.length, response);
      if (selected?.handled) return;
      if (selected?.status) {
        response.writeHead(selected.status, { "content-type": "application/json" });
        response.end(JSON.stringify({ error: { message: "Fixture provider refusal", type: "fixture_error" } }));
        return;
      }
      response.writeHead(200, { "content-type": "text/event-stream", "cache-control": "no-cache" });
      const emit = (delta, finish_reason = null) => response.write(`data: ${JSON.stringify({
        id: "fixture-completion", object: "chat.completion.chunk", created: 1, model: body.model,
        choices: [{ index: 0, delta, finish_reason }],
      })}\n\n`);
      emit({ role: "assistant", content: "" });
      if (selected?.tool) {
        const argumentsText = JSON.stringify(selected.tool.arguments);
        const half = Math.floor(argumentsText.length / 2);
        emit({ tool_calls: [{ index: 0, id: selected.tool.id ?? "call-fixture", type: "function", function: { name: selected.tool.name, arguments: "" } }] });
        emit({ tool_calls: [{ index: 0, function: { arguments: argumentsText.slice(0, half) } }] });
        emit({ tool_calls: [{ index: 0, function: { arguments: argumentsText.slice(half) } }] });
        emit({}, "tool_calls");
      } else {
        emit({ content: selected?.text ?? "Fixture answer." });
        emit({}, "stop");
      }
      response.write(`data: ${JSON.stringify({
        id: "fixture-completion", object: "chat.completion.chunk", choices: [],
        usage: { prompt_tokens: 30, completion_tokens: 20, total_tokens: 50 },
      })}\n\n`);
      response.end("data: [DONE]\n\n");
    } catch {
      if (!response.headersSent) response.writeHead(500);
      response.end();
    }
  });
  server.on("connection", (socket) => {
    connections.add(socket);
    socket.on("close", () => connections.delete(socket));
  });
  await new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", resolve);
  });
  const origin = `http://127.0.0.1:${server.address().port}`;
  return {
    baseUrl: `${origin}/v1`, origin, requests,
    async close() {
      for (const socket of connections) socket.destroy();
      await new Promise((resolve) => server.close(resolve));
    },
  };
}

export function lastUserText(body) {
  const user = body.messages.findLast((message) => message.role === "user");
  return typeof user?.content === "string" ? user.content : user?.content?.map((part) => part.text ?? "").join("");
}
