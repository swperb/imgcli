#!/usr/bin/env node
/**
 * imgcli-mcp — an MCP server that exposes the `imgcli` command-line image tool
 * as native tools for AI agents (Claude Desktop, Cursor, Smithery, etc.).
 *
 * Tools: convert_image, probe_image, list_filters.
 *
 * Transport is chosen at startup:
 *   - if PORT is set (e.g. Smithery's container runtime sets PORT=8081), the
 *     server speaks Streamable HTTP at /mcp;
 *   - otherwise it speaks stdio (npx imgcli-mcp, Claude Desktop, the Docker
 *     stdio check, etc.).
 *
 * The `imgcli` binary must be installed and on PATH (brew install swperb/tap/imgcli)
 * or pointed to via the IMGCLI_BIN environment variable. Arguments are passed as
 * an argv array (execFile, no shell) so there is no shell-injection surface.
 */
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { StreamableHTTPServerTransport } from "@modelcontextprotocol/sdk/server/streamableHttp.js";
import { z } from "zod";
import { execFile } from "node:child_process";
import { createServer as createHttpServer, type IncomingMessage, type ServerResponse } from "node:http";

const VERSION = "0.3.0";
const BIN = process.env.IMGCLI_BIN || "imgcli";

interface RunResult { code: number; stdout: string; stderr: string; }

function run(args: string[]): Promise<RunResult> {
  return new Promise((resolve) => {
    execFile(BIN, args, { timeout: 60_000, maxBuffer: 8 * 1024 * 1024 }, (err, stdout, stderr) => {
      const code = err && typeof (err as NodeJS.ErrnoException).code === "number"
        ? ((err as unknown as { code: number }).code)
        : err ? 1 : 0;
      resolve({ code, stdout: stdout ?? "", stderr: stderr ?? "" });
    });
  });
}

/** Parse imgcli's JSON line from stdout (success) or stderr (error). */
function parseJsonLine(text: string): unknown | null {
  const line = text.trim().split("\n").filter(Boolean).pop();
  if (!line) return null;
  try { return JSON.parse(line); } catch { return null; }
}

/** Build a fresh server with the three tools registered. A factory (not a
 * singleton) so the stateless HTTP transport can use one server per request. */
function buildServer(): McpServer {
  const server = new McpServer({ name: "imgcli", version: VERSION });

  server.registerTool(
    "convert_image",
    {
      title: "Convert / process an image",
      description:
        "Convert, resize, crop, rotate, filter, or composite an image with imgcli. " +
        "Output format is chosen from the output file extension (png/jpg/jpeg/bmp/tga/ppm/qoi). " +
        "Use `filters` for an ffmpeg-style comma-separated chain, e.g. " +
        '"scale=800:-1,grayscale,gblur=2". Call list_filters for the full catalogue.',
      inputSchema: {
        input: z.string().describe("Path to the input image file (or a generator like 'testsrc=640x480')."),
        output: z.string().describe("Path to write; the extension picks the format (png/jpg/bmp/tga/ppm/qoi)."),
        filters: z.string().optional().describe('Filtergraph, e.g. "scale=512:-1,grayscale". Omit for a plain format conversion.'),
        quality: z.number().int().min(1).max(100).optional().describe("JPEG quality 1-100 (default 90; ignored for non-JPEG)."),
        overwrite: z.boolean().optional().describe("Overwrite the output if it exists (default true)."),
      },
    },
    async ({ input, output, filters, quality, overwrite }) => {
      const args = ["--json", "-i", input];
      if (filters) args.push("-vf", filters);
      if (quality != null) args.push("-q", String(quality));
      args.push(overwrite === false ? "-n" : "-y", output);

      const { code, stdout, stderr } = await run(args);
      const result = parseJsonLine(code === 0 ? stdout : stderr) ?? {
        ok: false,
        error: (stderr || stdout || `imgcli exited with code ${code}`).trim(),
      };
      return {
        content: [{ type: "text", text: JSON.stringify(result) }],
        isError: code !== 0,
      };
    },
  );

  server.registerTool(
    "probe_image",
    {
      title: "Probe image dimensions",
      description: "Return the width, height, and channel count of an image without converting it.",
      inputSchema: { input: z.string().describe("Path to the input image file.") },
    },
    async ({ input }) => {
      const { code, stdout, stderr } = await run(["--json", "-info", "-i", input]);
      const result = parseJsonLine(code === 0 ? stdout : stderr) ?? {
        ok: false,
        error: (stderr || stdout || `imgcli exited with code ${code}`).trim(),
      };
      return { content: [{ type: "text", text: JSON.stringify(result) }], isError: code !== 0 };
    },
  );

  server.registerTool(
    "list_filters",
    {
      title: "List available filters",
      description: "List the full imgcli filter catalogue (geometry, colour, convolution, compositing).",
      inputSchema: {},
    },
    async () => {
      const { stdout } = await run(["-filters"]);
      return { content: [{ type: "text", text: stdout.trim() || "no output" }] };
    },
  );

  return server;
}

/** Read and JSON-parse a request body, with a hard size cap. */
function readJsonBody(req: IncomingMessage): Promise<unknown> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = [];
    let size = 0;
    req.on("data", (c: Buffer) => {
      size += c.length;
      if (size > 4 * 1024 * 1024) { req.destroy(); reject(new Error("request body too large")); return; }
      chunks.push(c);
    });
    req.on("end", () => {
      if (!chunks.length) { resolve(undefined); return; }
      try { resolve(JSON.parse(Buffer.concat(chunks).toString("utf8"))); }
      catch { reject(new Error("invalid JSON body")); }
    });
    req.on("error", reject);
  });
}

function rpcError(res: ServerResponse, status: number, message: string, allow?: string): void {
  if (res.headersSent) return;
  const headers: Record<string, string> = { "content-type": "application/json" };
  if (allow) headers["allow"] = allow;
  res.writeHead(status, headers);
  res.end(JSON.stringify({ jsonrpc: "2.0", error: { code: -32600, message }, id: null }));
}

/** Streamable HTTP transport (for Smithery's container runtime and any HTTP
 * MCP client). Stateless: one server+transport per POST, so nothing leaks
 * between callers. */
async function startHttp(port: number): Promise<void> {
  const httpServer = createHttpServer((req, res) => {
    void (async () => {
      const path = (req.url ?? "/").split("?")[0];

      // Liveness probe / friendly root for any non-MCP GET.
      if (req.method === "GET" && path !== "/mcp") {
        res.writeHead(200, { "content-type": "application/json" });
        res.end(JSON.stringify({ ok: true, server: "imgcli-mcp", version: VERSION }));
        return;
      }

      if (path !== "/mcp") { res.writeHead(404).end(); return; }

      if (req.method === "POST") {
        try {
          const body = await readJsonBody(req);
          const server = buildServer();
          const transport = new StreamableHTTPServerTransport({
            sessionIdGenerator: undefined,   // stateless
            enableJsonResponse: true,
          });
          res.on("close", () => { void transport.close(); void server.close(); });
          await server.connect(transport);
          await transport.handleRequest(req, res, body);
        } catch (e) {
          rpcError(res, 400, e instanceof Error ? e.message : "bad request");
        }
        return;
      }

      // GET (server-initiated SSE) and DELETE (session teardown) are unused in
      // stateless mode.
      rpcError(res, 405, "Method Not Allowed (stateless server: use POST)", "POST");
    })();
  });

  await new Promise<void>((resolve) => httpServer.listen(port, "0.0.0.0", resolve));
  process.stderr.write(`imgcli-mcp ready over HTTP on :${port}/mcp (binary: ${BIN})\n`);
}

async function main() {
  const port = process.env.PORT ? Number(process.env.PORT) : NaN;
  if (Number.isFinite(port) && port > 0) {
    await startHttp(port);
  } else {
    await buildServer().connect(new StdioServerTransport());
    // stderr is safe for logs; stdout is reserved for the MCP protocol.
    process.stderr.write(`imgcli-mcp ready over stdio (binary: ${BIN})\n`);
  }
}

main().catch((e) => {
  process.stderr.write(`imgcli-mcp fatal: ${e instanceof Error ? e.message : String(e)}\n`);
  process.exit(1);
});
