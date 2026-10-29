// Repeatable SB3 workloads. All costumes/sounds come from the supplied default
// project; no network extensions, audio loops or unbounded warp procedures.
const revision = 1;
const seed = 20261029;

export const performanceScenes = [
  {
    id: 'sprites', title: '多角色运动', seed,
    description: '独立角色持续移动、边缘反弹、旋转并切换造型，观察舞台绘制和编辑器交互。',
    levels: {
      light: {label: '轻负载', sprites: 40, clones: 0, expectedTargets: 41, iterationsPerCycle: 0},
      medium: {label: '中负载', sprites: 120, clones: 0, expectedTargets: 121, iterationsPerCycle: 0},
      heavy: {label: '重负载', sprites: 240, clones: 0, expectedTargets: 241, iterationsPerCycle: 0}
    }
  },
  {
    id: 'particles', title: '克隆体粒子', seed,
    description: '隐藏的发射器按固定序号创建克隆体；粒子运动、反弹、变色。重档保留在 Scratch 的 300 克隆上限内。',
    levels: {
      light: {label: '轻负载', sprites: 1, clones: 60, expectedTargets: 62, iterationsPerCycle: 0},
      medium: {label: '中负载', sprites: 1, clones: 150, expectedTargets: 152, iterationsPerCycle: 0},
      heavy: {label: '重负载', sprites: 1, clones: 280, expectedTargets: 282, iterationsPerCycle: 0}
    }
  },
  {
    id: 'scripts', title: '脚本计算与绘制', seed,
    description: '每个角色运行有界的整数运算自制积木，再更新位置与造型；计算过程最多循环 64 次，外层每轮显式让出执行。',
    levels: {
      light: {label: '轻负载', sprites: 20, clones: 0, expectedTargets: 21, iterationsPerCycle: 12},
      medium: {label: '中负载', sprites: 60, clones: 0, expectedTargets: 61, iterationsPerCycle: 32},
      heavy: {label: '重负载', sprites: 120, clones: 0, expectedTargets: 121, iterationsPerCycle: 64}
    }
  }
];

const number = (value, type = 4) => [1, [type, String(value)]];
const text = value => [1, [10, String(value)]];
const reporter = id => [3, id, [4, '0']];
const branch = id => [2, id];

function initialPosition(index) {
  // Integer-only formula: the same project always starts at the same positions.
  const value = (seed + index * 2654435761) >>> 0;
  return {x: value % 421 - 210, y: ((value >>> 9) + index * 53) % 301 - 150,
    direction: (index * 137 + 29) % 360};
}

function graph(prefix) {
  const blocks = {};
  const id = name => `${prefix}_${name}`;
  const add = (name, opcode, parent, next, inputs = {}, fields = {}, extra = {}) => {
    const key = id(name);
    blocks[key] = {opcode, next: next ? id(next) : null, parent: parent ? id(parent) : null,
      inputs, fields, shadow: false, topLevel: false, ...extra};
    return key;
  };
  const hat = (name, opcode, next, y = 40) => add(name, opcode, null, next, {}, {}, {topLevel: true, x: 40, y});
  const variable = (name, parent, variableId, label) => add(name, 'data_variable', parent, null, {}, {VARIABLE: [label, variableId]});
  const binary = (name, opcode, parent, left, right) => add(name, opcode, parent, null, {NUM1: left, NUM2: right});
  return {blocks, id, add, hat, variable, binary};
}

function moveLoop(g, {loop = 'forever', parent = 'flag', steps = 3, turn = 2, color = false} = {}) {
  g.add(loop, 'control_forever', parent, null, {SUBSTACK: branch(g.id('move'))});
  g.add('move', 'motion_movesteps', loop, 'bounce', {STEPS: typeof steps === 'number' ? number(steps) : steps});
  g.add('bounce', 'motion_ifonedgebounce', 'move', 'turn');
  g.add('turn', 'motion_turnright', 'bounce', color ? 'color' : 'costume', {DEGREES: number(turn)});
  if (color) g.add('color', 'looks_changeeffectby', 'turn', 'costume', {CHANGE: number(1)}, {EFFECT: ['COLOR', null]});
  g.add('costume', 'looks_nextcostume', color ? 'color' : 'turn', 'yield');
  // Zero waits yield to the next scheduler opportunity. No tight JS loop or
  // forever-warp block can monopolize the renderer thread.
  g.add('yield', 'control_wait', 'costume', null, {DURATION: number(0, 5)});
}

function spriteWorkload(sprite, index) {
  const g = graph(`perf_sprites_${index}`);
  g.hat('flag', 'event_whenflagclicked', 'home');
  g.add('home', 'motion_gotoxy', 'flag', 'heading', {X: number(sprite.x), Y: number(sprite.y)});
  g.add('heading', 'motion_pointindirection', 'home', 'effects', {DIRECTION: number(sprite.direction)});
  g.add('effects', 'looks_cleargraphiceffects', 'heading', 'forever');
  moveLoop(g, {parent: 'effects', steps: 2 + index % 4, turn: 1 + index % 3});
  sprite.blocks = g.blocks;
}

function particleWorkload(sprite, count) {
  const g = graph('perf_particles');
  const variableId = 'perf_particle_index';
  const label = '粒子序号';
  sprite.variables = {[variableId]: [label, 0]};
  sprite.visible = false;
  sprite.size = 18;
  g.hat('flag', 'event_whenflagclicked', 'hide');
  g.add('hide', 'looks_hide', 'flag', 'reset');
  g.add('reset', 'data_setvariableto', 'hide', 'spawn', {VALUE: number(0)}, {VARIABLE: [label, variableId]});
  g.add('spawn', 'control_repeat', 'reset', null, {TIMES: number(count, 6), SUBSTACK: branch(g.id('increment'))});
  g.add('increment', 'data_changevariableby', 'spawn', 'clone', {VALUE: number(1)}, {VARIABLE: [label, variableId]});
  g.add('clone', 'control_create_clone_of', 'increment', null, {CLONE_OPTION: [1, g.id('myself')]});
  g.add('myself', 'control_create_clone_of_menu', 'clone', null, {}, {CLONE_OPTION: ['_myself_', null]}, {shadow: true});

  // Clones copy their emitter's current local index at creation. This avoids
  // random block inputs and produces deterministic initial position/direction.
  g.hat('clone_start', 'control_start_as_clone', 'position', 230);
  g.add('position', 'motion_gotoxy', 'clone_start', 'direction', {X: reporter(g.id('x_subtract')), Y: reporter(g.id('y_subtract'))});
  g.binary('x_subtract', 'operator_subtract', 'position', reporter(g.id('x_mod')), number(220));
  g.binary('x_mod', 'operator_mod', 'x_subtract', reporter(g.id('x_multiply')), number(440));
  g.binary('x_multiply', 'operator_multiply', 'x_mod', reporter(g.id('x_index')), number(37));
  g.variable('x_index', 'x_multiply', variableId, label);
  g.binary('y_subtract', 'operator_subtract', 'position', reporter(g.id('y_mod')), number(160));
  g.binary('y_mod', 'operator_mod', 'y_subtract', reporter(g.id('y_multiply')), number(320));
  g.binary('y_multiply', 'operator_multiply', 'y_mod', reporter(g.id('y_index')), number(53));
  g.variable('y_index', 'y_multiply', variableId, label);
  g.add('direction', 'motion_pointindirection', 'position', 'hue', {DIRECTION: reporter(g.id('direction_mod'))});
  g.binary('direction_mod', 'operator_mod', 'direction', reporter(g.id('direction_multiply')), number(360));
  g.binary('direction_multiply', 'operator_multiply', 'direction_mod', reporter(g.id('direction_index')), number(137));
  g.variable('direction_index', 'direction_multiply', variableId, label);
  g.add('hue', 'looks_seteffectto', 'direction', 'show', {VALUE: reporter(g.id('hue_mod'))}, {EFFECT: ['COLOR', null]});
  g.binary('hue_mod', 'operator_mod', 'hue', reporter(g.id('hue_multiply')), number(200));
  g.binary('hue_multiply', 'operator_multiply', 'hue_mod', reporter(g.id('hue_index')), number(11));
  g.variable('hue_index', 'hue_multiply', variableId, label);
  g.add('show', 'looks_show', 'hue', 'forever');
  moveLoop(g, {parent: 'show', steps: 3, turn: 1, color: true});
  sprite.blocks = g.blocks;
}

function scriptWorkload(sprite, index, iterations) {
  const g = graph(`perf_scripts_${index}`);
  const variableId = `perf_value_${index}`;
  const label = '运算值';
  sprite.variables = {[variableId]: [label, (seed + index * 97) % 16777213]};
  g.hat('flag', 'event_whenflagclicked', 'reset');
  g.add('reset', 'data_setvariableto', 'flag', 'home', {VALUE: number((seed + index * 97) % 16777213)}, {VARIABLE: [label, variableId]});
  g.add('home', 'motion_gotoxy', 'reset', 'heading', {X: number(sprite.x), Y: number(sprite.y)});
  g.add('heading', 'motion_pointindirection', 'home', 'effects', {DIRECTION: number(sprite.direction)});
  g.add('effects', 'looks_cleargraphiceffects', 'heading', 'forever');
  g.add('forever', 'control_forever', 'effects', null, {SUBSTACK: branch(g.id('calculate'))});
  const mutation = {tagName: 'mutation', children: [], proccode: '有界计算一轮', argumentids: '[]', warp: 'true'};
  g.add('calculate', 'procedures_call', 'forever', 'move', {}, {}, {mutation: {...mutation}});
  g.add('move', 'motion_movesteps', 'calculate', 'bounce', {STEPS: number(2 + index % 4)});
  g.add('bounce', 'motion_ifonedgebounce', 'move', 'turn');
  g.add('turn', 'motion_turnright', 'bounce', 'costume', {DEGREES: number(1 + index % 3)});
  g.add('costume', 'looks_nextcostume', 'turn', 'yield');
  g.add('yield', 'control_wait', 'costume', null, {DURATION: number(0, 5)});

  g.add('definition', 'procedures_definition', null, 'repeat', {custom_block: [1, g.id('prototype')]}, {}, {topLevel: true, x: 420, y: 40});
  g.add('prototype', 'procedures_prototype', 'definition', null, {}, {}, {shadow: true,
    mutation: {...mutation, argumentnames: '[]', argumentdefaults: '[]'}});
  g.add('repeat', 'control_repeat', 'definition', null, {TIMES: number(iterations, 6), SUBSTACK: branch(g.id('assign'))});
  g.add('assign', 'data_setvariableto', 'repeat', null, {VALUE: reporter(g.id('mod'))}, {VARIABLE: [label, variableId]});
  g.binary('mod', 'operator_mod', 'assign', reporter(g.id('add')), number(16777213));
  g.binary('add', 'operator_add', 'mod', reporter(g.id('multiply')), number(1013904223));
  g.binary('multiply', 'operator_multiply', 'add', reporter(g.id('value')), number(1664525));
  g.variable('value', 'multiply', variableId, label);
  sprite.blocks = g.blocks;
}

export function buildPerformanceProject(baseProject, sceneId, level = 'medium') {
  const scene = performanceScenes.find(item => item.id === sceneId);
  if (!scene || !Object.hasOwn(scene.levels, level)) throw new Error('未知性能场景或负载档位。');
  const config = scene.levels[level];
  const project = structuredClone(baseProject);
  const stage = project.targets.find(target => target.isStage);
  const source = project.targets.find(target => !target.isStage);
  if (!stage || !source) throw new Error('性能场景需要默认舞台和角色。');
  stage.blocks = {};
  stage.comments = {};
  stage.tempo = 60;
  project.targets = [stage];
  project.monitors = [];
  project.extensions = [];
  project.meta = {...project.meta, agent: `SOUI Scratch performance/${revision}; scene=${sceneId}; level=${level}; seed=${seed}`};
  for (let index = 0; index < config.sprites; index++) {
    const sprite = structuredClone(source);
    Object.assign(sprite, initialPosition(index), {
      name: `${sceneId === 'particles' ? '粒子发射器' : sceneId === 'scripts' ? '计算角色' : '运动角色'}${String(index + 1).padStart(3, '0')}`,
      size: sceneId === 'particles' ? 18 : sceneId === 'scripts' ? 30 : 24,
      visible: sceneId !== 'particles', draggable: false, rotationStyle: 'all around',
      layerOrder: index + 1, currentCostume: index % source.costumes.length,
      variables: {}, lists: {}, broadcasts: {}, comments: {}, sounds: []
    });
    sprite.costumes.forEach((costume, costumeIndex) => {costume.name = `造型${costumeIndex + 1}`;});
    if (sceneId === 'sprites') spriteWorkload(sprite, index);
    else if (sceneId === 'particles') particleWorkload(sprite, config.clones);
    else scriptWorkload(sprite, index, config.iterationsPerCycle);
    project.targets.push(sprite);
  }
  return project;
}
