import { runReactTests } from '../react-suite.mjs';
await runReactTests(new URL('./', import.meta.url));
