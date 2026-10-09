import React from 'react';
import {createRoot} from 'react-dom/client';
import {Button as PrimeButton} from 'primereact/button';
import 'primereact/resources/themes/lara-light-blue/theme.css';
import {ReferenceReact} from '../../common/react-view';
const Button=(props:React.PropsWithChildren<{id?:string;disabled?:boolean;onClick?:()=>void;type?:'button'|'submit'}>)=>
  <PrimeButton {...props} label={typeof props.children==='string'?props.children:undefined}>{typeof props.children==='string'?undefined:props.children}</PrimeButton>;
createRoot(document.getElementById('app')!).render(<ReferenceReact profile="compat-react18-miniblink" Button={Button}/>);
