enum cslModule {
  legacy = "CslLegacy.js",
  wasm = "CslWasm.js",
  bundle = "CslWasmBundle.js"
}

const CslModule: CslModuleFactoryLegacy | CslModuleFactoryWasm | CslModuleFactoryBundle = require('./' + cslModule.bundle);

process.stdin.setEncoding('utf8');

const path = require('path');

function replaceRootDir(originalPath: string, newRoot: string): string {
  const parsed = path.parse(originalPath);

  // Break the path into segments, excluding the root
  const relativeSegments = originalPath
    .slice(parsed.root.length) // Remove the root from the original path
    .split(path.sep)
    .filter(Boolean); // Remove empty strings

  // Join the new root with the remaining path
  return path.posix.join(newRoot, ...relativeSegments);
}

CslModule({
  noInitialRun: true
})
  .then((Module: any) => {
    let args = process.argv.slice(2);
    if (args.length >= 2 && args[0] === '--test') {
      const mountDir = '/mounted';
      Module.FS.mkdir(mountDir);
      Module.FS.mount(Module.FS.filesystems.NODEFS, { root: '/' }, mountDir);
      let input = args[1];
      const absInputPath = path.resolve(input!);
      args[1] = replaceRootDir(absInputPath, mountDir);
    }
    else if (args.length == 3 && args[0] === '--langsvr') {
      if (args[2].startsWith('--clientProcessId=')) {
        args = args.slice(0, 2);
      }
    }

    Module.print = (text: string) => {
      process.stdout.write(text + '\n');
    };

    Module.printErr = (err: string) => {
      process.stderr.write('[CSL WASM error] ' + err + '\n');
    };

    Module.callMain(args);

    Module.FS.quit();
    process.exit(0);
  })
  .catch((err: any) => {
    process.stderr.write('[CSL module error] ' + err + '\n');
  });
