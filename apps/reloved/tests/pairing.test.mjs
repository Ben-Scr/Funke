import test from 'node:test';
import assert from 'node:assert/strict';
import {PAIRING_TTL_MS,PairingRegistry} from '../extension/pairing.js';

const sender = (overrides={}) => ({
 url:'http://127.0.0.1:5173/settings',
 tab:{id:17},
 frameId:0,
 documentId:'document-one',
 ...overrides,
});
const memoryStorage = () => {
 const values={};
 return {
  async get(key){return {[key]:values[key]};},
  async set(items){Object.assign(values,items);},
  async remove(key){delete values[key];},
 };
};

test('Approval binds an unpredictable token to one top-level app document',async()=>{
 const now=1000;
 const pairings=new PairingRegistry({storage:memoryStorage(),now:()=>now,createToken:()=> 'a'.repeat(32)});
 const approval=await pairings.approve(sender());
 assert.deepEqual(approval,{token:'a'.repeat(32),expiresAt:now+PAIRING_TTL_MS});
 assert.deepEqual(await pairings.authorize(sender(),approval.token),{tabId:17,documentId:'document-one'});
});

test('Unpaired, navigated, expired and incorrectly tokened documents are rejected',async()=>{
 let now=1000;
 const pairings=new PairingRegistry({storage:memoryStorage(),now:()=>now,createToken:()=> 'b'.repeat(32)});
 await assert.rejects(pairings.authorize(sender(),'b'.repeat(32)),/Diese Seite verbinden/);
 const {token}=await pairings.approve(sender());
 await assert.rejects(pairings.authorize(sender({documentId:'document-two'}),token),/Diese Seite verbinden/);
 await assert.rejects(pairings.authorize(sender({tab:{id:18}}),token),/Diese Seite verbinden/);
 await assert.rejects(pairings.authorize(sender(),'c'.repeat(32)),/Diese Seite verbinden/);
 now+=PAIRING_TTL_MS;
 await assert.rejects(pairings.authorize(sender(),token),/Diese Seite verbinden/);
});

test('Lookalike origins and subframes cannot receive or use approval',async()=>{
 const pairings=new PairingRegistry({storage:memoryStorage(),createToken:()=> 'd'.repeat(32)});
 for(const untrusted of [
   sender({url:'http://127.0.0.1:5173.evil.example/'}),
   sender({url:'https://example.com/'}),
   sender({frameId:1}),
 ]) await assert.rejects(pairings.approve(untrusted),/nicht für Reloved Connect freigegeben/);
});

test('Closing a tab revokes its current approval',async()=>{
 const pairings=new PairingRegistry({storage:memoryStorage(),createToken:()=> 'e'.repeat(32)});
 const {token}=await pairings.approve(sender());
 await pairings.removeTab(17);
 await assert.rejects(pairings.authorize(sender(),token),/Diese Seite verbinden/);
});
