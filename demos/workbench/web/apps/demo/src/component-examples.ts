import { h, ref, defineComponent, onErrorCaptured, onBeforeUnmount, withDirectives, vShow } from 'vue';
import { componentLoaders } from './component-loaders';
import { KitButton, KitSwitch, KitSlider } from './kit-ui';
import { ElButtonGroup } from 'element-plus/es/components/button/index';
import 'element-plus/theme-chalk/el-button-group.css';

const E: Record<string, any> = {ElButton:KitButton, ElButtonGroup, ElSwitch:KitSwitch, ElSlider:KitSlider};
const pending = new Map<string, Promise<void>>([
  ['Button',Promise.resolve()], ['Switch',Promise.resolve()], ['Slider',Promise.resolve()],
]);
export function loadExampleComponents(name: string): Promise<void> {
  if (pending.has(name)) return pending.get(name)!;
  const loader = Object.prototype.hasOwnProperty.call(componentLoaders,name) ? componentLoaders[name] : undefined;
  if (typeof loader !== 'function') return Promise.reject(new Error(`No local loader for ${name}`));
  const promise = loader().then(module => {
    Object.assign(E,module.default);
    if (!E['El' + name]) throw new Error(`Component export missing: El${name}`);
  }).catch(error => {
    pending.delete(name); throw error;
  });
  pending.set(name,promise);
  return promise;
}

// Component families (child components appear in their parent's working example).
export const catalog = [
  ['基础','Button','按钮'],['基础','Icon','图标'],['基础','Link','链接'],['基础','Text','文本'],['基础','Scrollbar','滚动条'],['基础','Space','间距'],['基础','Divider','分割线'],['基础','Row','布局'],['基础','Container','容器'],['基础','ConfigProvider','配置'],
  ['表单','Input','输入框'],['表单','InputNumber','计数器'],['表单','InputTag','标签输入'],['表单','Autocomplete','自动补全'],['表单','Mention','提及'],['表单','Checkbox','复选框'],['表单','Radio','单选框'],['表单','Switch','开关'],['表单','Slider','滑块'],['表单','Rate','评分'],['表单','Select','选择器'],['表单','SelectV2','虚拟选择器'],['表单','Cascader','级联选择'],['表单','CascaderPanel','级联面板'],['表单','DatePicker','日期选择'],['表单','DatePickerPanel','日期面板'],['表单','TimePicker','时间选择'],['表单','TimeSelect','时间选项'],['表单','ColorPicker','颜色选择'],['表单','ColorPickerPanel','颜色面板'],['表单','Transfer','穿梭框'],['表单','Form','表单'],['表单','Upload','文件选择'],
  ['数据','Avatar','头像'],['数据','Badge','徽标'],['数据','Card','卡片'],['数据','Calendar','日历'],['数据','Carousel','轮播'],['数据','Collapse','折叠面板'],['数据','Descriptions','描述列表'],['数据','Empty','空状态'],['数据','Image','图片'],['数据','ImageViewer','图片查看'],['数据','Pagination','分页'],['数据','Progress','进度条'],['数据','Result','结果'],['数据','Skeleton','骨架屏'],['数据','Statistic','统计'],['数据','Countdown','倒计时'],['数据','Table','表格'],['数据','TableV2','虚拟表格'],['数据','Tag','标签'],['数据','CheckTag','可选标签'],['数据','Timeline','时间线'],['数据','Tree','树'],['数据','TreeV2','虚拟树'],['数据','TreeSelect','树选择'],['数据','Watermark','水印'],
  ['导航','Affix','固钉'],['导航','Anchor','锚点'],['导航','Backtop','回到顶部'],['导航','Breadcrumb','面包屑'],['导航','Dropdown','下拉菜单'],['导航','Menu','菜单'],['导航','PageHeader','页头'],['导航','Steps','步骤条'],['导航','Tabs','标签页'],['导航','Segmented','分段控制'],['导航','Splitter','分隔面板'],
  ['反馈','Alert','提示'],['反馈','Dialog','对话框'],['反馈','Drawer','抽屉'],['反馈','Popconfirm','确认气泡'],['反馈','Popover','气泡'],['反馈','Tooltip','文字提示'],['反馈','Tour','引导'],['反馈','Loading','加载'],['反馈','Message','消息'],['反馈','MessageBox','确认框'],['反馈','Notification','通知'],['反馈','CollapseTransition','展开过渡'],['数据','InfiniteScroll','无限滚动'],
].map(([group, name, title]) => ({ group, name, title }));
const all = E as unknown as Record<string, any>;
const c = (name: string, props: any = {}, children?: any) => {
  const component = all['El' + name];
  if (!component) throw new Error(`Component dependency not loaded: El${name}`);
  return h(component, props, children);
};
const rows = Array.from({length: 30}, (_, i) => ({ id: i + 1, name: `本地文件 ${i + 1}`, state: '示例' }));
const tree = [{id:1,label:'工作空间',children:[{id:2,label:'界面'},{id:3,label:'原生服务'}]}];
const options = [{value:'ui',label:'界面',children:[{value:'button',label:'按钮'}]},{value:'native',label:'原生'}];
const poster = 'data:image/svg+xml,' + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="320" height="160"><rect width="320" height="160" fill="#e6efff"/><circle cx="160" cy="80" r="40" fill="#2f6fed"/></svg>');

export const Example = defineComponent({
  props: { name: {type: String, required: true} },
  setup(props) {
    const feedbackHandles = new Set<{ close(): void }>();
    let disposed = false;
    let confirming = false;
    const feedback = (create: (onClose: () => void) => { close(): void }) => {
      if (disposed) return;
      let handle: { close(): void } | undefined;
      let closed = false;
      handle = create(() => { closed = true; if (handle) feedbackHandles.delete(handle); });
      if (!closed) feedbackHandles.add(handle);
    };
    onBeforeUnmount(() => {
      disposed = true;
      for (const handle of Array.from(feedbackHandles)) handle.close();
      feedbackHandles.clear();
    });
    const value = ref<any>(''), open = ref(false), count = ref(4), choice = ref('界面'), checks = ref(['界面']);
    const update = (v: any) => { value.value = v; };
    const button = (label: string, fn = () => { count.value++; }) => c('Button', {onClick:fn}, () => label);
    const model = (fallback: any) => ({modelValue:value.value === '' ? fallback : value.value,'onUpdate:modelValue':update});
    return () => {
      const n = props.name;
      switch (n) {
        case 'Button': return h('div', [c('ButtonGroup',{},()=>[button('点击计数'),c('Button',{type:'primary',onClick:()=>count.value++},()=>`已点击 ${count.value}`)]),c('Button',{disabled:true},()=> '禁用')]);
        case 'Icon': return c(n,{size:32,color:'#2f6fed'},()=>h('svg',{viewBox:'0 0 24 24'},[h('path',{d:'M3 7h7l2 3h9v10H3z',fill:'currentColor'})]));
        case 'Link': return c(n,{href:'#gallery-top',underline:'always'},()=> '返回组件页顶部（本地锚点）');
        case 'Text': return c(n,{type:'primary',size:'large'},()=> '成熟组件，真实交互');
        // Fixed-size example: do not install observers for a size that never changes.
        case 'Scrollbar':
          if (import.meta.env.MODE === 'gallery-experiment' && import.meta.env.VITE_PLAIN_SCROLL === 'true') {
            return h('div',{'data-implementation':'native-overflow-experiment',style:'height:220px;overflow:auto'},rows.map(row=>h('p',row.name)));
          }
          return c(n,{height:'220px',noresize:true},()=>rows.map(row=>h('p',row.name)));
        case 'Space': return c(n,{size:20,wrap:true},()=>[button('操作一'),button('操作二'),button('操作三')]);
        case 'Divider': return h('div',[h('p','上方内容'),c(n,{},()=> '分组'),h('p','下方内容')]);
        case 'Row': return c(n,{gutter:16},()=>[c('Col',{span:12},()=>c('Card',{},()=> '左列 12')),c('Col',{span:12},()=>c('Card',{},()=> '右列 12'))]);
        case 'Container': return c(n,{},()=>[c('Aside',{width:'120px'},()=> '侧栏'),c('Container',{},()=>[c('Header',{},()=> '标题'),c('Main',{},()=> '工作区域'),c('Footer',{},()=> '状态栏')])]);
        case 'ConfigProvider': return c(n,{size:'small'},()=>button('配置为小尺寸'));
        case 'Input': return c(n,{...model(''),placeholder:'输入文字，支持清空',clearable:true});
        case 'InputNumber': return c(n,{...model(3),min:0,max:20});
        case 'InputTag': return c(n,{...model(['Vue','C++']),placeholder:'输入后按 Enter'});
        case 'Autocomplete': return c(n,{...model(''),placeholder:'输入 ui',fetchSuggestions:(q:string,done:any)=>done(['ui','native','bridge'].filter(x=>x.includes(q)).map(x=>({value:x})))});
        case 'Mention': return c(n,{...model(''),options:[{label:'界面',value:'ui'},{label:'原生',value:'native'}],placeholder:'输入 @'});
        case 'Checkbox': return c('CheckboxGroup',{modelValue:checks.value,'onUpdate:modelValue':(v:any)=>checks.value=v},()=>['界面','原生'].map(v=>c('Checkbox',{value:v},()=>v)));
        case 'Radio': return c('RadioGroup',{modelValue:choice.value,'onUpdate:modelValue':(v:any)=>choice.value=v},()=>['界面','原生'].map(v=>c('Radio',{value:v},()=>v)));
        case 'Switch': return c(n,model(true));
        case 'Slider': return c(n,{...model(40),showTooltip:false});
        case 'Rate': return c(n,{...model(3),showScore:true});
        case 'Select': return c(n,{...model('ui'),placeholder:'选择模块'},()=>options.map(x=>c('Option',{label:x.label,value:x.value})));
        case 'SelectV2': return c(n,{...model('ui'),options});
        case 'Cascader': case 'CascaderPanel': return c(n,{...model([]),options});
        case 'DatePicker': case 'DatePickerPanel': return c(n,{...model(new Date(2026,8,7)),type:'date'});
        case 'TimePicker': return c(n,{...model(new Date(2026,8,7,9,30))});
        case 'TimeSelect': return c(n,{...model('09:30'),start:'09:00',end:'18:00',step:'00:30'});
        case 'ColorPicker': case 'ColorPickerPanel': return c(n,model('#2f6fed'));
        case 'Transfer': return c(n,{...model([2]),data:rows.slice(0,8).map(r=>({key:r.id,label:r.name}))});
        case 'Form': return c(n,{model:{name:value.value},labelWidth:80},()=>[c('FormItem',{label:'名称'},()=>c('Input',model(''))),c('FormItem',{},()=>button('提交预览',()=>feedback(onClose=>E.ElMessage({type:'success',message:'本地表单预览，不执行原生任务',onClose}))))]);
        case 'Upload': return c(n,{autoUpload:false,action:'',limit:3}, {default:()=>button('选择文件（不上传）'),tip:()=>h('p','仅预览文件列表，不读取内容、不访问网络。')});
        case 'Avatar': return c(n,{size:64},()=> 'UI');
        case 'Badge': return c(n,{value:count.value},()=>button('增加数量'));
        case 'Card': return c(n,{}, {header:()=> '工作空间',default:()=> '本地示例卡片',footer:()=>button('操作')});
        case 'Calendar': return c(n,model(new Date(2026,8,7)));
        case 'Carousel': return c(n,{height:'180px',autoplay:false},()=>[1,2,3].map(i=>c('CarouselItem',{},()=>h('div',{class:'example-slide'},`手动切换 ${i}`))));
        case 'Collapse': return c(n,model(['1']),()=>[c('CollapseItem',{name:'1',title:'标准 SDK'},()=> '页面通过统一契约调用原生能力。'),c('CollapseItem',{name:'2',title:'离线资源'},()=> '脚本与样式都从本地加载。')]);
        case 'Descriptions': return c(n,{column:2,border:true},()=>[c('DescriptionsItem',{label:'界面'},()=> 'Vue'),c('DescriptionsItem',{label:'服务'},()=> 'C++')]);
        case 'Empty': return c(n,{description:'没有数据，可以选择其他组件'});
        case 'Image': return c(n,{src:poster,style:'width:320px;height:160px',previewSrcList:[poster]});
        case 'ImageViewer': return h('div',[button('打开图片查看器',()=>open.value=true),open.value?c(n,{urlList:[poster],onClose:()=>open.value=false}):null]);
        case 'Pagination': return c(n,{currentPage:count.value,'onUpdate:currentPage':(v:number)=>count.value=v,pageSize:10,total:200,layout:'prev, pager, next, total'});
        case 'Progress': return h('div',[c(n,{percentage:count.value*10%101}),button('更新进度')]);
        case 'Result': return c(n,{icon:'success',title:'组件预览',subTitle:'此结果仅演示样式，不代表原生任务成功'});
        case 'Skeleton': return h('div',[button('切换加载',()=>open.value=!open.value),c(n,{loading:!open.value,rows:3,animated:false},()=> '内容已显示')]);
        case 'Statistic': return c(n,{value:123456,title:'示例数量'});
        case 'Countdown': return c(n,{value:Date.now()+60000,title:'一分钟预览倒计时'});
        case 'Table': return c(n,{data:rows,height:280,stripe:true},()=>[c('TableColumn',{prop:'id',label:'编号',width:80}),c('TableColumn',{prop:'name',label:'文件'}),c('TableColumn',{prop:'state',label:'状态'})]);
        case 'TableV2': return c(n,{data:rows,width:540,height:280,columns:[{key:'id',dataKey:'id',title:'编号',width:100},{key:'name',dataKey:'name',title:'文件',width:300}]});
        case 'Tag': return h('div',[c(n,{closable:true,onClose:()=>open.value=true},()=>open.value?'关闭事件已触发':'可关闭标签')]);
        case 'CheckTag': return c(n,{checked:open.value,onChange:(v:boolean)=>open.value=v},()=> '可选标签');
        case 'Timeline': return c(n,{},()=>['创建','执行','完成'].map((v,i)=>c('TimelineItem',{timestamp:`阶段 ${i+1}`},()=>v)));
        case 'Tree': case 'TreeV2': return c(n,{data:tree,height:240,showCheckbox:true,defaultExpandAll:true});
        case 'TreeSelect': return c(n,{...model(undefined),data:tree,checkStrictly:true,props:{value:'id'}});
        case 'Watermark': return c(n,{content:'本地预览'},()=>h('div',{style:'height:220px'},'水印示例'));
        case 'Affix': return h('div',{style:'height:300px;overflow:auto'},[c(n,{offset:30},()=>button('固钉示例')),h('div',{style:'height:500px'},'向下滚动观察定位')]);
        case 'Anchor': return h('div',[c(n,{},()=>c('AnchorLink',{href:'#anchor-example',title:'跳转到示例位置'})),h('p',{id:'anchor-example'},'示例位置')]);
        case 'Backtop': return h('div',{id:'backtop-example',style:'height:200px;overflow:auto'},[h('div',{style:'height:1200px'},'在这个区域向下滚动，右下角出现返回顶部'),c(n,{target:'#backtop-example',visibilityHeight:50,right:260})]);
        case 'Breadcrumb': return c(n,{},()=>['开发套件','组件库','预览'].map(v=>c('BreadcrumbItem',{},()=>v)));
        case 'Dropdown': return c(n,{onCommand:(v:string)=>feedback(onClose=>E.ElMessage({message:v,onClose}))}, {default:()=>button('打开菜单'),dropdown:()=>c('DropdownMenu',{},()=>['查看','刷新'].map(v=>c('DropdownItem',{command:v},()=>v)))});
        case 'Menu': return c(n,{defaultActive:'1'},()=>[c('MenuItem',{index:'1'},()=> '工作空间'),c('SubMenu',{index:'2'},{title:()=> '设置',default:()=>c('MenuItem',{index:'2-1'},()=> '外观')})]);
        case 'PageHeader': return c(n,{title:'返回',content:'组件示例',onBack:()=>feedback(onClose=>E.ElMessage({message:'返回事件已触发',onClose}))});
        case 'Steps': return h('div',[c(n,{active:count.value%4},()=>['选择','执行','完成'].map(v=>c('Step',{title:v}))),button('下一步')]);
        case 'Tabs': return c(n,model('ui'),()=>['ui','native'].map(v=>c('TabPane',{label:v,name:v},()=> `${v} 内容`)));
        case 'Segmented': return c(n,{...model('界面'),options:['界面','原生','状态']});
        case 'Splitter': return c(n,{style:'height:220px'},()=>[c('SplitterPanel',{size:'40%'},()=> '拖动中间分隔条'),c('SplitterPanel',{},()=> '右侧内容')]);
        case 'Alert': return c(n,{title:'本地提示',description:'可关闭的提示组件',type:'info',showIcon:true});
        case 'Dialog': case 'Drawer': return h('div',[button('打开'+(n==='Dialog'?'对话框':'抽屉'),()=>open.value=true),c(n,{modelValue:open.value,'onUpdate:modelValue':(v:boolean)=>open.value=v,title:'本地组件预览',appendToBody:true,width:460},()=>h('div',[h('p','没有调用系统能力。'),button('关闭',()=>open.value=false)]))]);
        case 'Popconfirm': return c(n,{title:'确定执行预览操作？',onConfirm:()=>feedback(onClose=>E.ElMessage({type:'success',message:'已确认',onClose}))},{reference:()=>button('确认操作')});
        case 'Popover': return c(n,{title:'详情',content:'本地气泡内容',trigger:'click'},{reference:()=>button('显示气泡')});
        case 'Tooltip': return c(n,{content:'这是本地提示',placement:'top'},()=>button('鼠标移到这里'));
        case 'Tour': return h('div',[h('div',{id:'tour-example-target'},'被引导的控件'),button('开始引导',()=>open.value=true),c(n,{modelValue:open.value,'onUpdate:modelValue':(v:boolean)=>open.value=v},()=>c('TourStep',{target:'#tour-example-target',title:'组件引导',description:'这个区域是引导目标'}))]);
        case 'Message': return button('显示消息',()=>feedback(onClose=>E.ElMessage({type:'success',message:'操作反馈示例',onClose})));
        case 'MessageBox': return button('打开确认框',()=>{
          if (confirming || disposed) return;
          confirming = true;
          feedback(onClose => {
            const container = document.createElement('div');
            container.className = 'example-message-box-owner';
            document.body.appendChild(container);
            // Use this instance's normal close action so its Promise, scroll
            // lock and Element Plus bookkeeping settle without closing peers.
            const close = () => container.querySelector<HTMLButtonElement>('.el-message-box__headerbtn')?.click();
            E.ElMessageBox.confirm('仅预览确认交互，不执行系统操作。','确认',{
              appendTo:container,showClose:true,distinguishCancelAndClose:true,
            }).then(()=>feedback(done=>E.ElMessage({message:'已确认',onClose:done})))
              .catch(()=>{})
              .finally(()=>{ confirming=false; onClose(); container.remove(); });
            return {close};
          });
        });
        case 'Notification': return button('显示通知',()=>feedback(onClose=>E.ElNotification({title:'本地通知',message:'组件预览，不是系统通知',duration:3000,onClose})));
        case 'Loading': return h('div',[button('切换加载',()=>open.value=!open.value),withDirectives(h('div',{style:'height:180px'},'加载目标内容'),[[E.ElLoading.directive,open.value]])]);
        case 'CollapseTransition': return h('div',[button('展开 / 收起',()=>open.value=!open.value),c(n,{},()=>withDirectives(h('div',{class:'example-collapse',style:'background:#eef3ff'},[h('div',{style:'padding:20px'},'可折叠内容')]),[[vShow,open.value]]))]);
        case 'InfiniteScroll': return withDirectives(h('div',{style:'height:200px;overflow:auto','infinite-scroll-disabled':count.value>=50},Array.from({length:count.value},(_,i)=>h('p',`条目 ${i+1}`))),[[E.ElInfiniteScroll,()=>{count.value=Math.min(50,count.value+5);}]]);
        default: return h('p','尚未提供此组件示例，不能视为兼容。');
      }
    };
  }
});

export const ExampleBoundary = defineComponent({
  emits:['failure'],
  setup(_, {slots,emit}) {
    const error = ref('');
    onErrorCaptured((cause) => { error.value=String(cause); emit('failure', error.value); return false; });
    return () => error.value ? h('div',{class:'gallery-error',role:'alert'},['当前组件运行失败；此项不能标记为兼容。',h('pre',error.value)]) : slots.default?.();
  }
});
