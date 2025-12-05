import * as vscode from "vscode";
import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  TransportKind,
} from "vscode-languageclient/node";

let client: LanguageClient;

export function activate(context: vscode.ExtensionContext) {
  const serverOptions: ServerOptions = {
    // module: context.asAbsolutePath("core/csl.js"),
    // options: { cwd: context.asAbsolutePath("../../node/out") },
    command: context.extensionPath + "/core/csl",
    args: ["--langsvr"],
    // transport: TransportKind.pipe,
    // transport: {
    //   kind: TransportKind.socket,
    //   port: 2087,
    // },
    transport: TransportKind.stdio,
  };

  // Client options
  const clientOptions: LanguageClientOptions = {
    documentSelector: [{ scheme: "file", language: "csl" }],
    synchronize: {
      fileEvents: vscode.workspace.createFileSystemWatcher("**/*.csl"),
    },
  };

  client = new LanguageClient(
    "CslLanguageService",
    "CSL Language Service",
    serverOptions,
    clientOptions
  );

  context.subscriptions.push(
    vscode.commands.registerCommand(
      "csl.generateHtmlDoc",
      async (resource?: vscode.Uri) => {
        let targetUri: vscode.Uri | undefined;
        let text: string | undefined;
        if (resource && resource.scheme === "file") {
          targetUri = resource;
          const buf = await vscode.workspace.fs.readFile(resource);
          text = Buffer.from(buf).toString("utf8");
        } else {
          const editor = vscode.window.activeTextEditor;
          if (!editor || editor.document.languageId !== "csl") {
            vscode.window.showErrorMessage(
              "Open a CSL file or invoke from explorer."
            );
            return;
          }
          targetUri = editor.document.uri;
          text = editor.document.getText();
        }
        const dirPick = await vscode.window.showOpenDialog({
          canSelectFolders: true,
          canSelectFiles: false,
          canSelectMany: false,
          title: "Select output folder for CSL HTML docs",
        });
        if (!dirPick || !dirPick.length) {
          return;
        }
        const outDir = dirPick[0];
        try {
          const result = await client.sendRequest<any>("csl/generateHtmlDoc", {
            textDocument: { uri: targetUri.toString(), text },
          });
          const entries: [string, string][] = Object.entries(result || {});
          for (const [relPath, content] of entries) {
            const segments = relPath.split("/").filter(Boolean);
            const folder = segments.slice(0, -1);
            if (folder.length) {
              await vscode.workspace.fs.createDirectory(
                vscode.Uri.joinPath(outDir, ...folder)
              );
            }
            const target = vscode.Uri.joinPath(outDir, ...segments);
            await vscode.workspace.fs.writeFile(
              target,
              Buffer.from(content, "utf8")
            );
          }
          const indexUri = vscode.Uri.joinPath(outDir, "index.html");
          vscode.env.openExternal(vscode.Uri.file(indexUri.fsPath));
        } catch (e: any) {
          vscode.window.showErrorMessage(String(e?.message || e));
        }
      }
    )
  );

  client
    .start()
    .then(() => {
      vscode.window.showInformationMessage("CSL Language Service Ready");
    })
    .catch((reason) => {
      console.error("Cannot start csl language service: ", reason);
    });

  return;
}

export function deactivate() {
  if (!client) {
    return;
  }
  client.stop().catch((reason) => {
    console.error("Cannot stop csl language service: ", reason);
  });
  return;
}
