'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const repo='C:/SOUI-SDK-development/preview14-scheduler',file=path.join(repo,'third_party/sources/miniblink/content/web_impl_win/WebThreadImpl.cpp');
const raw=fs.readFileSync(file),text=raw.toString('latin1'),eol=text.includes('\r\n')?'\r\n':'\n';
const before=`        size_t selectedIndex = 0;
        for (size_t i = 1; i < m_timerHeap.size(); ++i) {
            WebTimerBase* candidate = m_timerHeap[i];
            if (!m_willExit && candidate->m_nextFireTime > fireTime)
                continue;
            WebTimerBase* selected = m_timerHeap[selectedIndex];
            if (candidate->m_priority > selected->m_priority
                || (candidate->m_priority == selected->m_priority
                    && candidate->m_nextFireTime < selected->m_nextFireTime)) {
                selectedIndex = i;
            }
        }`.replaceAll('\n',eol);
const after=`        const size_t selectedIndex = task_scheduling::selectReadyTimer(
            m_timerHeap, fireTime, m_willExit, [](const WebTimerBase* value) {
                return task_scheduling::ReadyTask{value->m_nextFireTime, value->m_priority};
            });`.replaceAll('\n',eol);
assert.equal(text.split(before).length,2);const include='#include "content/web_impl_win/WebThreadImpl.h"';assert.equal(text.split(include).length,2);
fs.writeFileSync(path.join(__dirname,'WebThreadImpl-before.cpp'),raw,{flag:'wx'});
fs.writeFileSync(file,Buffer.from(text.replace(include,include+eol+'#include "content/web_impl_win/TaskSchedulingPolicy.h"').replace(before,after),'latin1'));
fs.copyFileSync('C:/SOUI-SDK-production-validation/preview13-full-source-build-attempt2-20260926/Capture-Command.cjs',path.join(__dirname,'Capture-Command.cjs'),fs.constants.COPYFILE_EXCL);
