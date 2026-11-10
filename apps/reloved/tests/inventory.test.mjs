import test from 'node:test';
import assert from 'node:assert/strict';
import {normalizeItems,demoItems} from '../lib/inventory.ts';
import {profileParts,collectInventory,toItem} from '../extension/api.js';

test('Backup round-trip preserves photos, prices and queue state',()=>{
 assert.deepEqual(normalizeItems(JSON.parse(JSON.stringify(demoItems))),demoItems.map(i=>({...i,url:undefined})));
});
test('Invalid imports reject atomically, including duplicate identities and invalid prices',()=>{
 for(const invalid of [[demoItems[0],demoItems[0]],[{...demoItems[0],price:-1}],[{...demoItems[0],price:NaN}],[{...demoItems[0],id:'x'.repeat(151)}],[{...demoItems[0],status:'unknown'}],{}])assert.throws(()=>normalizeItems(invalid));
});
test('Untrusted photo and listing URLs are filtered',()=>{
 const [result]=normalizeItems([{...demoItems[0],photos:['javascript:alert(1)','data:image/svg+xml;base64,abc','https://images.example.org/test.jpg'],url:'https://www.vinted.de.evil.com/items/1'}]);
 assert.equal(result.url,undefined);assert.deepEqual(result.photos,['https://images.example.org/test.jpg']);
});
test('Profile input is restricted to supported Vinted HTTPS hosts',()=>{
 assert.deepEqual(profileParts('https://www.vinted.de/member/12345-test'),{origin:'https://www.vinted.de',id:'12345'});
 for(const u of ['http://www.vinted.de/member/1','https://www.vinted.de.evil.org/member/1','https://example.com/member/1','https://www.vinted.de/items/1'])assert.throws(()=>profileParts(u));
});
test('Pagination follows total_pages and keeps every item exactly once',async()=>{
 const paths=[];const result=await collectInventory(async path=>{paths.push(path);const page=Number(new URL(path,'https://example.org').searchParams.get('page'));return {items:[{id:page,title:`Article ${page}`,price:{amount:'12.50'}}],pagination:{total_pages:3}};},'https://www.vinted.de','123');
 assert.equal(result.length,3);assert.equal(paths.length,3);assert.equal(result[0].price,12.5);
});
test('Repeated pagination and changed API schemas fail instead of saving incomplete data',async()=>{
 await assert.rejects(collectInventory(async()=>({items:[{id:1,title:'A',price:10}],pagination:{total_pages:3}}),'https://www.vinted.de','123'),/wiederholt/);
 await assert.rejects(collectInventory(async()=>({data:[]}), 'https://www.vinted.de','123'),/Antwortformat/);
 await assert.rejects(collectInventory(async()=>{throw new Error('HTTP 403');},'https://www.vinted.de','123'),/403/);
});
test('API mapping keeps sold and closed items out of active inventory',()=>{
 assert.equal(toItem({id:12,title:'Sold',price:3,is_sold:true},'https://www.vinted.de').status,'sold');
 assert.equal(toItem({id:13,title:'Hidden',price:4,is_hidden:true},'https://www.vinted.de').status,'archived');
});
