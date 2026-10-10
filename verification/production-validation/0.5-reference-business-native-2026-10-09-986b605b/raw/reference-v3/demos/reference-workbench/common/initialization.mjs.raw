// Actual controller initialization; every awaited native boundary checks document closure.
export async function initializeReference(steps) {
  if(!steps.alive())return false;
  const client=await steps.connect();
  if(!steps.alive())return false;
  steps.adoptClient(client);
  if(!steps.alive())return false;
  const tools=await steps.discover(client);
  if(!steps.alive())return false;
  steps.validateTools(tools);
  if(!steps.alive())return false;
  const description=await steps.describe(client);
  if(!steps.alive())return false;
  steps.createTask(client,description);
  if(!steps.alive())return false;
  await steps.probe(client);
  if(!steps.alive())return false;
  steps.ready();
  return true;
}
