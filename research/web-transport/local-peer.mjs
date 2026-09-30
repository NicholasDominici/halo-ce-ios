import {encodeFrame,decodeFrame,TYPES} from "./frames.mjs";
const status=document.querySelector("#status"),display=document.querySelector("#results");
const report={checks:[],errors:[]};
const log=(name,value)=>{report.checks.push({name,value});display.textContent=JSON.stringify(report,null,2);};
const assert=(condition,message)=>{if(!condition)throw new Error(message);};
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const pc=new RTCPeerConnection({iceServers:[]});
let reliable,unreliable,polling=true;
const received=[],remoteCandidates=[];let processing=Promise.resolve();
window.haloLocalFixture={report,received,pc,channels:{}};
async function command(value){await fetch("/command",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(value)});}
function channel(dc){
  window.haloLocalFixture.channels[dc.label]=dc;
  dc.binaryType="arraybuffer";
  if(dc.label==="halo-reliable-v1")reliable=dc;else if(dc.label==="halo-unreliable-v1")unreliable=dc;else throw new Error("Unexpected channel");
  dc.onmessage=event=>{
    let frame;
    try {frame=decodeFrame(event.data);}catch(error){report.errors.push(error.message);return;}
    received.push(frame);
    // Echo streams opened by the native socket peer. Client test uses ID 0x12345678.
    if(frame.type===TYPES.STREAM_DATA && frame.streamId!==0x12345678)reliable.send(encodeFrame(frame));
  };
}
pc.ondatachannel=event=>channel(event.channel);
pc.onicecandidate=event=>{if(event.candidate)void command({type:"candidate",candidate:event.candidate.candidate,mid:event.candidate.sdpMid});};
async function handle(signal){
  if(signal.event==="description"){
    await pc.setRemoteDescription({type:signal.detail,sdp:signal.value});
    while(remoteCandidates.length)await pc.addIceCandidate(remoteCandidates.shift());
    if(signal.detail==="offer"){await pc.setLocalDescription(await pc.createAnswer());await command({type:"description",descriptionType:"answer",sdp:pc.localDescription.sdp});}
  }else if(signal.event==="candidate"){
    const candidate={candidate:signal.value,sdpMid:signal.detail};
    if(pc.remoteDescription)await pc.addIceCandidate(candidate);else remoteCandidates.push(candidate);
  }
}
let lastState;
async function poll(){
  while(polling){
    lastState=await(await fetch("/signal")).json();
    for(const signal of lastState.signals)processing=processing.then(()=>handle(signal));
    await processing;
    const error=lastState.events.find(value=>value.event==="error" || value.event==="command-error");if(error)throw new Error(JSON.stringify(error));
    await sleep(15);
  }
}
async function until(predicate,label,ms=12000){const deadline=performance.now()+ms;while(!predicate()){if(performance.now()>deadline)throw new Error("Timed out: "+label);await sleep(10);}}
async function run(){
  const initial=await(await fetch("/signal")).json();
  for(const signal of initial.signals)processing=processing.then(()=>handle(signal));
  if(!initial.nativeOffer){
    channel(pc.createDataChannel("halo-reliable-v1",{ordered:true}));
    channel(pc.createDataChannel("halo-unreliable-v1",{ordered:false,maxRetransmits:0}));
    await pc.setLocalDescription(await pc.createOffer());await command({type:"description",descriptionType:"offer",sdp:pc.localDescription.sdp});
  }
  const errors=poll().catch(error=>{report.errors.push(error.message);polling=false;});
  await until(()=>reliable?.readyState==="open" && unreliable?.readyState==="open","two channels");
  assert(reliable.ordered && reliable.maxRetransmits===null,"Reliable policy");
  assert(!unreliable.ordered && unreliable.maxRetransmits===0,"Unreliable policy");
  log("DTLS/SCTP connection and both channel policies",initial.nativeOffer?"native offer":"browser offer");
  status.textContent="Connected. Testing native socket delivery…";
  const payload=Uint8Array.from({length:1500},(_,i)=>i%251);
  unreliable.send(encodeFrame({type:TYPES.DATAGRAM,sourcePort:5152,destinationPort:5151,payload}));
  await until(()=>received.some(f=>f.type===TYPES.DATAGRAM),"UDP echo");
  const udp=received.find(f=>f.type===TYPES.DATAGRAM);
  assert(udp.sourcePort===5151 && udp.destinationPort===5152 && udp.payload.length===payload.length && udp.payload.every((v,i)=>v===payload[i]),"Datagram bytes/ports changed");
  log("1500-byte datagram echo",true);
  reliable.send(encodeFrame({type:TYPES.STREAM_OPEN,streamId:0x12345678,sourcePort:5152,destinationPort:5150}));
  const stream=Uint8Array.from({length:250000},(_,i)=>(i*31)%251);
  for(let offset=0;offset<stream.length;offset+=16000)reliable.send(encodeFrame({type:TYPES.STREAM_DATA,streamId:0x12345678,payload:stream.subarray(offset,offset+16000)}));
  await until(()=>received.filter(f=>f.type===TYPES.STREAM_DATA && f.streamId===0x12345678).reduce((n,f)=>n+f.payload.length,0)===stream.length,"250 KB stream echo");
  const echoed=new Uint8Array(stream.length);let offset=0;
  for(const frame of received.filter(f=>f.type===TYPES.STREAM_DATA && f.streamId===0x12345678)){echoed.set(frame.payload,offset);offset+=frame.payload.length;}
  assert(echoed.every((v,i)=>v===stream[i]),"Reliable stream bytes/order changed");log("250000-byte segmented stream echo",true);
  reliable.send(encodeFrame({type:TYPES.STREAM_CLOSE,streamId:0x12345678}));
  await until(()=>lastState?.events.some(value=>value.event==="eof"),"native EOF");log("Remote stream close / native EOF",true);
  await command({type:"native-send",payload:"native to browser socket"});
  await until(()=>lastState?.events.some(value=>value.event==="native-reply"),"native-initiated stream");
  assert(lastState.events.find(value=>value.event==="native-reply").payload==="native to browser socket","Native socket reply mismatch");
  log("Native-initiated stream / browser reply",true);
  polling=false;await errors;assert(!report.errors.length,"Signaling or native error");
  report.pass=true;status.textContent="PASS — native/browser packet delivery verified";status.className="pass";
  display.textContent=JSON.stringify(report,null,2);
  await fetch("/result",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(report)});
}
run().catch(async error=>{
  polling=false;report.errors.push(error.message);report.pass=false;status.textContent="FAIL — "+error.message;status.className="fail";
  display.textContent=JSON.stringify(report,null,2);await fetch("/result",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(report)});
});
