export function lessonProject(project, lesson = 'circle') {
  const result = structuredClone(project);
  const sprite = result.targets.find(target => !target.isStage);
  result.targets.forEach(target => {
    target.costumes?.forEach((costume,index) => {costume.name = target.isStage ? `背景${index+1}` : `造型${index+1}`;});
    target.sounds?.forEach((sound,index) => {sound.name = target.isStage ? `音效${index+1}` : '喵';});
  });
  sprite.name = '小猫'; sprite.x = 0; sprite.y = 0; sprite.direction = 90;
  const block = (opcode, next, parent, inputs = {}, fields = {}) => ({opcode, next, parent, inputs, fields, shadow: false, topLevel: false});
  const blocks = {};
  const hat = (id, opcode, next, fields, y) => {blocks[id] = {...block(opcode, next, null, {}, fields), topLevel:true, x:50, y};};
  if (lesson === 'circle') {
    hat('flag', 'event_whenflagclicked', 'home', {}, 45);
    blocks.home = block('motion_gotoxy', 'repeat', 'flag', {X:[1,[4,'-100']],Y:[1,[4,'0']]});
    blocks.repeat = block('control_repeat', 'say', 'home', {TIMES:[1,[6,'20']],SUBSTACK:[2,'move']});
    blocks.move = block('motion_movesteps', 'turn', 'repeat', {STEPS:[1,[4,'15']]});
    blocks.turn = block('motion_turnright', 'wait', 'move', {DEGREES:[1,[4,'18']]});
    blocks.wait = block('control_wait', null, 'turn', {DURATION:[1,[5,'0.08']]});
    blocks.say = block('looks_sayforsecs', null, 'repeat', {MESSAGE:[1,[10,'我转了一圈！试试修改角度吧。']],SECS:[1,[5,'2']]});
  } else if (lesson === 'keys') {
    hat('flag', 'event_whenflagclicked', 'home', {}, 35);
    blocks.home = block('motion_gotoxy', null, 'flag', {X:[1,[4,'0']],Y:[1,[4,'0']]});
    [['right arrow','motion_changexby','DX',15],['left arrow','motion_changexby','DX',-15],['up arrow','motion_changeyby','DY',15],['down arrow','motion_changeyby','DY',-15]].forEach(([key,opcode,input,value], index) => {
      hat(`key${index}`, 'event_whenkeypressed', `step${index}`, {KEY_OPTION:[key,null]}, 170+index*125);
      blocks[`step${index}`] = block(opcode, null, `key${index}`, {[input]:[1,[4,String(value)]]});
    });
  }
  sprite.blocks = blocks;
  return result;
}
