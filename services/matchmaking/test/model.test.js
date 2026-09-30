import test from "node:test";
import assert from "node:assert/strict";
import { initialState, createTicket, publishInvite, pollTicket, cancelTicket, sweep, WAIT_MS, LEASE_MS } from "../src/model.js";
const input={playlist:"bloodgulch-slayer-8",compatibility:"test-build:map-sha",name:"Spartan"};
const identity=i=>({id:`player-${i}`,token:`secret-${i}`,room:`room-${i}`});
const invite="halo://join/"+"a".repeat(44);
test("eight people share one room; ninth starts a new room",()=>{
  const state=initialState(),tickets=[];
  for(let i=0;i<9;i++)tickets.push(createTicket(state,input,identity(i),1000,`ip-${i}`));
  assert.equal(tickets[0].role,"host");assert.equal(tickets[1].role,"join");
  assert.equal(tickets[7].room,tickets[0].room);assert.notEqual(tickets[8].room,tickets[0].room);
  assert.equal(tickets[8].role,"host");assert.equal(tickets[7].botSeats,0);
});
test("only authenticated room members get host invite",()=>{
  const state=initialState();createTicket(state,input,identity(0),0);createTicket(state,input,identity(1),0);
  assert.throws(()=>publishInvite(state,"player-1","secret-1",invite,1),{status:403});
  assert.throws(()=>pollTicket(state,"player-1","wrong",1),{status:404});
  publishInvite(state,"player-0","secret-0",invite,2);
  assert.equal(pollTicket(state,"player-1","secret-1",3).invite,invite);
  assert.equal(pollTicket(state,"player-0","secret-0",3).invite,null);
});
test("map/build incompatibility and elapsed deadline create separate rooms",()=>{
  const state=initialState(),one=createTicket(state,input,identity(0),0);
  const different=createTicket(state,{...input,compatibility:"other-map"},identity(1),1);
  const late=createTicket(state,input,identity(2),WAIT_MS+1);
  assert.notEqual(one.room,different.room);assert.notEqual(one.room,late.room);
});
test("canceling the host invalidates the room and every joining ticket",()=>{
  const state=initialState();createTicket(state,input,identity(0),0);createTicket(state,input,identity(1),0);
  cancelTicket(state,"player-0","secret-0",1);
  assert.equal(pollTicket(state,"player-1","secret-1",2).state,"expired");
  assert.equal(Object.keys(state.rooms).length,0);
});
test("join cancellation frees a seat without stopping the host",()=>{
  const state=initialState();createTicket(state,input,identity(0),0);createTicket(state,input,identity(1),0);
  cancelTicket(state,"player-1","secret-1",1);
  assert.equal(pollTicket(state,"player-0","secret-0",2).humans,1);
  assert.equal(pollTicket(state,"player-0","secret-0",2).botSeats,7);
});
test("expired hosts cannot be joined; polling keeps a live host leased",()=>{
  const state=initialState();createTicket(state,input,identity(0),0);
  pollTicket(state,"player-0","secret-0",LEASE_MS-1);sweep(state,LEASE_MS+1);
  assert.equal(Object.keys(state.rooms).length,1);
  sweep(state,2*LEASE_MS);assert.equal(Object.keys(state.rooms).length,0);
});
test("state survives serialization and does not expose tokens in polling",()=>{
  let state=initialState();createTicket(state,input,identity(0),0);
  state=JSON.parse(JSON.stringify(state));
  assert.equal(pollTicket(state,"player-0","secret-0",1).role,"host");
  assert.equal(pollTicket(state,"player-0","secret-0",1).token,undefined);
});
test("reject bad input, malformed invites, and a changed invite",()=>{
  const state=initialState();
  assert.throws(()=>createTicket(state,{...input,name:"x\n"},identity(0),0),{status:400});
  createTicket(state,input,identity(0),0);
  assert.throws(()=>publishInvite(state,"player-0","secret-0","https://example.com",1),{status:400});
  publishInvite(state,"player-0","secret-0",invite,1);
  assert.throws(()=>publishInvite(state,"player-0","secret-0","halo://join/"+"b".repeat(44),2),{status:409});
});
test("rate limit applies to repeated anonymous searches",()=>{
  const state=initialState();for(let i=0;i<10;i++)createTicket(state,input,identity(i),0);
  assert.throws(()=>createTicket(state,input,identity(11),1),{status:429});
  createTicket(state,input,identity(12),60_001);
});
