/*
 *  REF language client for VS Code and its forks (Cursor, Antigravity, VSCodium).
 *
 *  The heavy lifting lives in the `referee-lsp` server (see the repo's
 *  `lsp/main.cpp`); this file is only the thin client that launches it and hands
 *  its stdio to VS Code's LanguageClient. VS Code then drives the protocol —
 *  didOpen/didChange out, publishDiagnostics in — and paints the squiggles.
 *
 *  The server binary is located from the `referee.lsp.path` setting (default
 *  `referee-lsp`, i.e. found on PATH). Point it at an absolute build path, or set
 *  it to `docker` with `referee.lsp.args` for a containerized server.
 */
import * as vscode from 'vscode';
import {
    Executable,
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
} from 'vscode-languageclient/node';

let client: LanguageClient | undefined;

// VS Code does not expand ${workspaceFolder} / ${userHome} in settings an
// extension reads itself (only launch.json / tasks.json get that), so do it
// here — the Docker server command needs the workspace path in its -v mount.
function expandVars(s: string): string {
    const folder = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath ?? '';
    return s
        .replace(/\$\{workspaceFolder\}/g, folder)
        .replace(/\$\{userHome\}/g, process.env.HOME ?? process.env.USERPROFILE ?? '');
}

function makeClient(): LanguageClient {
    const cfg = vscode.workspace.getConfiguration('referee');
    const command = expandVars(cfg.get<string>('lsp.path', 'referee-lsp'));
    const args = cfg.get<string[]>('lsp.args', []).map(expandVars);

    // Executable transport defaults to stdio, which is what the server speaks.
    const server: Executable = { command, args };
    const serverOptions: ServerOptions = { run: server, debug: server };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: 'ref' }],
        outputChannelName: 'REF Language Server',
    };

    return new LanguageClient('referee', 'REF Language Server', serverOptions, clientOptions);
}

export function activate(context: vscode.ExtensionContext): void {
    client = makeClient();
    context.subscriptions.push(client);
    client.start();

    // Restart on demand — handy after rebuilding the server or changing the path.
    context.subscriptions.push(
        vscode.commands.registerCommand('referee.restartServer', async () => {
            await client?.stop();
            client = makeClient();
            context.subscriptions.push(client);
            await client.start();
            void vscode.window.showInformationMessage('REF language server restarted.');
        }),
    );
}

export function deactivate(): Thenable<void> | undefined {
    return client?.stop();
}
