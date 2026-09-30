/* Explicit loopback-only fixture: never calls the author's signaling service. */
import http from "node:http";
import fs from "node:fs";
import path from "node:path";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
const here=path.dirname(fileURLToPath(import.meta.url));
const port=Number(process.env.HALO_WEB_TEST_PORT || 9030);
const nativeOffer=process.argv.includes("--native-offer");
const externalNative=process.argv.includes("--external-native");
const native=externalNative?null:spawn(process.env.HALO_WEB_TEST_BINARY || path.resolve(here,"../../build/web/native/halo-web-rtc-probe"),nativeOffer?["offer"]:[],{stdio:["pipe","pipe","inherit"]});
let pending="",signals=[],commands=[],events=[],result=null;
native?.stdout.on("data",chunk=>{
  pending+=chunk.toString();let end;
  while((end=pending.indexOf("\n"))>=0){
    const line=pending.slice(0,end);pending=pending.slice(end+1);
    try{const event=JSON.parse(line);if(["description","candidate"].includes(event.event))signals.push(event);else {events.push(event);console.log(JSON.stringify(event));}}catch{}
  }
});
native?.on("error",error=>{events.push({event:"error",value:error.message});console.error(error.message);});
native?.on("exit",code=>events.push({event:"exit",code}));
const server=http.createServer(async(request,response)=>{
  try{
    if(request.headers.host!==`127.0.0.1:${port}`){response.writeHead(403).end();return;}
    const url=new URL(request.url,`http://127.0.0.1:${port}`);
    if(request.headers.origin && request.headers.origin!==`http://127.0.0.1:${port}`){response.writeHead(403).end();return;}
    response.setHeader("Cache-Control","no-store");
    if(request.method==="GET" && url.pathname==="/"){response.setHeader("Content-Type","text/html");response.end(fs.readFileSync(path.join(here,"local-peer.html")));}
    else if(request.method==="GET" && ["/frames.mjs","/local-peer.mjs"].includes(url.pathname)){response.setHeader("Content-Type","text/javascript");response.end(fs.readFileSync(path.join(here,url.pathname.slice(1))));}
    else if(request.method==="GET" && url.pathname==="/signal"){response.setHeader("Content-Type","application/json");response.end(JSON.stringify({protocol:"halo-web-local-fixture-v1",nativeOffer,externalNative,signals:url.searchParams.get("side")==="native"?[]:signals.splice(0),commands:url.searchParams.get("side")==="native"?commands.splice(0):[],events,result}));}
    else if(request.method==="POST" && ["/command","/result"].includes(url.pathname)){
      let size=0,chunks=[];for await(const chunk of request){size+=chunk.length;if(size>128*1024)throw new Error("Request too large");chunks.push(chunk);}
      const value=JSON.parse(Buffer.concat(chunks).toString());
      if(url.pathname==="/command"){
        if(externalNative && url.searchParams.get("side")==="native"){
          if(["description","candidate"].includes(value.event))signals.push(value);else {events.push(value);console.log(JSON.stringify(value));}
        } else if(externalNative)commands.push(value);
        else native.stdin.write(JSON.stringify(value)+"\n");
      }
      else {result=value;console.log("BROWSER RESULT "+JSON.stringify(value));}
      if(signals.length>128 || commands.length>128 || events.length>1024)throw new Error("Fixture queue limit exceeded; restart the fixture");
      response.setHeader("Content-Type","application/json");response.end("{}");
    }else response.writeHead(404).end();
  }catch(error){response.writeHead(400).end(error.message);}
});
server.listen(port,"127.0.0.1",()=>console.log(`Local fixture http://127.0.0.1:${port}/ (${nativeOffer?"native":"browser"} offer)`));
function stop(){native?.stdin.end();server.closeAllConnections();server.close();setTimeout(()=>native?.kill("SIGTERM"),1000).unref();}
process.on("SIGINT",stop);process.on("SIGTERM",stop);
