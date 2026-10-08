#!/usr/bin/env node
'use strict';

const { run } = require('./actiongen/generate.cjs');
run(process.argv.slice(2)).catch(error => {
  console.error(`actiongen: ${error.message}`);
  process.exitCode = 1;
});
