# Continuation — 2026-10-16 03:25 local

Read this before C:/work/sm05-long-quota-1/CHECKPOINT-source24-and-quota.md, then C:/work/sm05-legal-text-fix-1/CHECKPOINT-source24-2.md. User still wants finish0.5.0 then publish, no more monitor questions. No commit/push/release/goal/automation/subagent created. Candidate C:/work/sm05-release-050 remains frozen and unchanged during this continuation. Separate Git C:/work/sm05-release-050-git-2. Source24 manifest SHA52e19555127cff41afabb4ee9e7b0af662630cbdfce35f270d0d4753473d1cf4 remains current. C about73GiBfree; F/Dnearfull. Do not touch unrelatedQEMU.

## Only active execution

exec **95062**, C:/work/sm05-legal-text-fix-1/run-source24-2.cjs. Progress C:/work/sm05-source24-pipeline-2/pipeline.ndjson. Web and bothx64 builds/CTest passed, x64Release UnitOnly natural0/0 **408264ms,1362ownedprocesses,Job0,nocleanup**. x86Debug Build passed **775664ms,770processes,natural0/0** at03:18:18; x86Debug native CTest passed at03:19:21. **Current x86Release Build started03:19:21**. Remaining pipeline still ReleaseCTest/unit, alldesktop/Modern/readers, bothSDK, visiblecachecomparison, both100reloadcalibration+quota, dependencies,referenceWeb/native/controller/GUI. Do not duplicate or alter inputs. No functions.wait pending. All UIA probe sessions listed below are CLOSED.

## Existing longquota proposal still pending

C:/work/sm05-long-quota-1/PROPOSAL-v2.json is still the correct 13-file proposal, **260purechecksPASS**, notadopted. WHITESPACE2.json inspected: all13 comparisons exit1 with emptyoutput (=differentfiles, no whitespaceerrors with CRLFallowed). Raw pin mismatch regression fixed beforesealing. Broadcache budget stillNE. This continuation did not change thosefiles.

## NEW UIA implementation prepared outside frozen candidate

Folder **C:/work/sm05-uia-events-1**, sealed **PROPOSAL.json**. Sixfiles underproposal/; currentbeforepins andafterpins. No candidateadoption. Exactprovider andtests botharchitectures actuallycompiled/run. Headeronlycomments and public CMakecasewiring updatedafterfinitebuilds; no ABI layoutchange.

Discovered existing windows_uia_provider::publish updatedstate but raised no UIA events. Referencebusiness hasnoUIAprojectionyet; thispacket is state-notification SDKfix, notthatintegration.

Implementation:
- Dynamic system UIAutomationCore loading adds PropertyChanged,StructureChanged,AutomationEvent; no staticimportorhigherOSbaseline.
- publish remainsatomic/bounded and posts existingownerwake; actualnotificationsdrained in pump, atmost16eventAPIcalls alongsideexisting8businesscommands.
- Coalesces unstartedintermediatepublicstates; startedbatchfinishesbeforelatest. Three bounded snapshots, finite recordpointereventtable; no newthreads/timers. Name/enabled/offscreen/bounds/status/value/selection/focus androotChildrenInvalidated forstructure/identity/order.
- State lock neverheldacrossUIAAPI. api_borrow andnotificationcallbackborrow retainHWND/owner andblockrecursivepump. close revokes/discardsunstartednotifications; actualborrowedclose remainsretryable. Errorreturnedfrompump withremainingeventkeptforlaterretry; acceptedstateisnotrolledback.
- Skip notificationtargets removed/reincarnated whilebatchwaspending. secretvalue/statusstillrejected,noValuepattern/event. No Invoked/action-completioneventclaim.

Files: sdk/accessibility/uia/windows_uia_provider.cpp; sdk/include/soui/runtime/accessibility/windows_uia_provider.h; tests/native/windows_uia_provider_tests.cpp; tests/native/uia/CMakeLists.txt; newADR0109 and docs/acceptance/uia-state-notifications.md. ADRstatus explicitly proposed/notadopted. Atadoption must also appendlinkstoADRREADME,CHANGELOG,docs/testing whilepreservinglongquota-v2 changes; these threeoverlaps deliberatelynotcopiedinthisproposal.

Actualvalidation:
- **x64-green3-VERIFIED.json and x86-green1-VERIFIED.json**: each12records (1build+11separatecases), allnatural0/0,Job0,noforce. **22nativecasesPASS**, existing8+new3 property-events/tree-events/revoked-events.
- property-events actualMTAclientsubscribes, ownerpublishespublicvalue, callbackverifiessender/property/payload/currentreadback, thenunchangedpublishmustremainquiet. tree-events receivesrootChildrenInvalidated andactualownerfocus. revoked-events rejectssecretvalueandclosesbeforequeuedpubliceventcanemit; noValueevent.
- **x64-red3** isactualtargetRED: originalprovider nativeownerpublish succeeds butsubscribedclienthasnoevent. Root1/1, ownconhostcleaned, notnatural. red1 CMakebackslashpathcompilefailure and red2 in-processCUIASetValuefailure are NOTtargetRED.
- green1/diagnostic1 initiallyfailedexactcallbackcount: oneValuechangereceivedtwocorrectcallbacks. **diagnostic2** tracedexactlyoneproviderraiseandtwoclientcallbacks; schedulingmadecount1attestinstantbutsecondcallbackfollowed. Finaltestrequires>=1correctdeliveryandseparateno-changequiet; no hiddenSDKtrace remains. Diagnostic teststderr intentionallyprintsnonsecretfixedsamplevalue.
- green2 `all` ran7casesincludingproperty-events thenfailedoldCOM-uninitializedcase aftertrueUIAactivation. PublicCTest alreadyisolatescases; newrun.cjs `each` correctlyusesseparateprocesses. Allpriorfailuresretained. Do notcombineCOMinitializationnegativeintoUIAactivatedprocess.
- **WHITESPACE.json** all6comparisons exit1/emptyoutput withCRLFallowed, no actualwhitespaceerrors.
- Nativehelper source: run.cjs, build.ps1, native/CMakeLists.txt; usesactualcurrentaction_contract.cpp andcandidatepublicheaders, providerfrombeforeorproposal, explicitLLVM22/MSVC142/SDK19041. No fullcurrentSDK/nativeGUI/targetmatrix/releasequalification implied.

Pendingcode-review considerations for thispacket: readchanges directly; coalescing isexplicitandmaydropintermediateunnotifiedstates. Notifications are statechanges only; actualbusinessprojection/screenreader/Invokedremainsopen. Do notexpandclaimfrom22cases.

## New Learning

Captured throughFroot runtime: **learn-2026-10-15-uia-provider-call-versus-client-delivery-24a8c6d071**, recurrenceuia-provider-call-versus-client-delivery. TraceactualproviderAPIcalls, verifycorrectclientdelivery/readbackandno-changequiet, donotassertexactcallbackcount.

Runtime `learning capture --help` incorrectlyreturnedsummaryrequired. Documentedmanualflags were recoveredfrom .specify/templates/passive-skills/spec-kit-project-learning/SKILL.md lines63–73 and actualcapture succeeded. No uninvokedspworkflowentered.

## Next useful implementation investigation (NOT STARTED)

Standard Build.ps1 hasWithWebView2 but noWithUia; standard-build.cjs lacks --with-uia. SDKoption SOUI_BUILD_UIA isOFFbydefault. Current187CTest/mainSDKpipeline doesNOTbuildoptionalUIA, so22privatecases do notreplacefinaloptionalmoduleinstallationqualification.

Potential nextslice: expose explicit -WithUia / --with-uia in public build, bind optional soui_uia.lib in build-evidence.cjs (optionalLibraries currentlyonlyWebView2), testtoggle/rejection andarchive identity. LLVMobserver seesallNinja compile/linkedges generically; withWebView2 specialhandling atObserve-LlvmBuild.ps1:781 isexternaldependency/preload only, UIA hasnoexternalSDK. Needreadactualoption/graphvalidation beforechanging. Existing tests/sdk/standard-build.test.cjs and llvm-evidence-readers.test.cjs providepurefixtures. Do NOTmodifyfrozencandidate while95062runs; ifimplement,useanotherprivateproposal andbatchadoptionlater.

Mostrecentcommentary03:23: x86Debugbuildand187testsPASS, Releasebuilding; UIA22PASSsavedforlaterintegration. Keepworking, nofinalyet. Remainingfullreleasegatesunchanged inpriorcheckpoint.
