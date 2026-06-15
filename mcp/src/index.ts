#!/usr/bin/env node
/**
 * imgcli-mcp — an MCP server that exposes the `imgcli` command-line image tool
 * as native tools for AI agents (Claude Desktop, Cursor, etc.).
 *
 * Tools: convert_image, probe_image, list_filters.
 *
 * The `imgcli` binary must be installed and on PATH (brew install swperb/tap/imgcli)
 * or pointed to via the IMGCLI_BIN environment variable. Arguments are passed as
 * an argv array (execFile, no shell) so there is no shell-injection surface.
 */
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { execFile } from "node:child_process";

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

const server = new McpServer({ name: "imgcli", version: "0.2.0" });

server.registerTool(
  "convert_image",
  {
    title: "Convert / process an image",
    description:
      "Convert, resize, crop, rotate, filter, or composite an image with imgcli. " +
      "Output format is chosen from the output file extension (png/jpg/jpeg/bmp/tga/ppm). " +
      "Use `filters` for an ffmpeg-style comma-separated chain, e.g. " +
      '"scale=800:-1,grayscale,gblur=2". Call list_filters for the full catalogue.',
    inputSchema: {
      input: z.string().describe("Path to the input image file (or a generator like 'testsrc=640x480')."),
      output: z.string().describe("Path to write; the extension picks the format (png/jpg/bmp/tga/ppm)."),
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

async function main() {
  await server.connect(new StdioServerTransport());
  // stderr is safe for logs; stdout is reserved for the MCP protocol.
  process.stderr.write(`imgcli-mcp ready (binary: ${BIN})\n`);
}

main().catch((e) => {
  process.stderr.write(`imgcli-mcp fatal: ${e instanceof Error ? e.message : String(e)}\n`);
  process.exit(1);
});
