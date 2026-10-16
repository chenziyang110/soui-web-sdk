const fs=require('node:fs');const file=__dirname+'/proposal/tests/sdk/llvm-evidence-readers.test.cjs';fs.appendFileSync(file,fs.readFileSync(__dirname+'/configure-tests.inc'));
