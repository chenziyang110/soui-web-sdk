export function initializeReference<Client,Tools,Description>(steps:{
  alive():boolean;
  connect():Promise<Client>;
  adoptClient(client:Client):void;
  discover(client:Client):Promise<Tools>;
  validateTools(tools:Tools):void;
  describe(client:Client):Promise<Description>;
  createTask(client:Client,description:Description):void;
  probe(client:Client):Promise<unknown>;
  ready():void;
}):Promise<boolean>;
