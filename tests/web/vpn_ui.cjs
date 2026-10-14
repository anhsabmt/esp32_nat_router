/* UI/network regression fixtures; hardware is tested separately. */
const {webkit}=require('playwright'),http=require('http'),fs=require('fs');
const assert=require('assert/strict'),path=require('path');
const pageHTML=fs.readFileSync(path.join(__dirname,'../../components/http_server/pages/vpn_profiles.html'),'utf8');
const key='AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE=';
const conf=`[Interface]\nPrivateKey = ${key}\nAddress = 10.2.0.2/32\nDNS = 10.2.0.1\n[Peer]\nPublicKey = ${key}\nEndpoint = test.example.test:51820\nAllowedIPs = 0.0.0.0/0\n`;
let mode='ok',selected=-1,profiles=[],active=0,maximum=0;
const sockets=new Set();
const server=http.createServer(async(req,res)=>{
  if(req.url!=='/api/vpn-profiles') {
    res.setHeader('Content-Type','text/html; charset=utf-8');
    res.end(mode==='script_error'?pageHTML.replace('window.vpnUIReady=true;refresh();','BROKEN )'):pageHTML);return;
  }
  active++;maximum=Math.max(maximum,active);res.on('close',()=>active--);
  if(mode==='hang_get' && req.method==='GET')return;
  if(mode==='hang_post' && req.method==='POST')return;
  if(mode==='auth'){res.writeHead(403);res.end('Forbidden');return;}
  if(mode==='bad_json'){res.end('not JSON');return;}
  if(mode==='slow')await new Promise(resolve=>setTimeout(resolve,300));
  if(req.method==='POST') {
    let raw='';for await(const part of req)raw+=part;
    const body=JSON.parse(raw);
    assert.equal(req.headers['x-vpn-request'],'1');
    if(body.action==='router')selected=-1;
    else if(body.action==='select'||body.action==='connect')selected=body.id;
    else if(body.action==='save') {
      assert(body.conf.includes('[Interface]'));
      profiles.push({id:profiles.length,name:body.name,endpoint:'test.example.test',port:51820,address:'10.2.0.2',dns:'10.2.0.1'});
    }
    res.end(JSON.stringify({ok:true,msg:'Saved'}));return;
  }
  res.end(JSON.stringify({ok:true,selected,state:selected<0?'router':'disconnected',killswitch:selected>=0,heap:60000,max:8,profiles}));
});
server.on('connection',s=>{sockets.add(s);s.on('close',()=>sockets.delete(s))});
(async()=>{
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const browser=await webkit.launch({headless:true}),context=await browser.newContext({viewport:{width:390,height:844},isMobile:true});
  const errors=[],page=await context.newPage(),url=`http://127.0.0.1:${server.address().port}/vpn`;
  page.on('pageerror',e=>errors.push(e.message));page.on('dialog',d=>d.accept());
  try {
    // Deliberately remove newer APIs: file import and profile rendering still work.
    await page.addInitScript(()=>{File.prototype.text=undefined;Element.prototype.replaceChildren=undefined});
    await page.goto(url);await page.waitForFunction(()=>document.querySelector('#state').textContent.includes('Internet trực tiếp'));
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
    await page.locator('#files').setInputFiles([{name:'A.conf',mimeType:'text/plain',buffer:Buffer.from(conf)},{name:'B.conf',mimeType:'text/plain',buffer:Buffer.from(conf)}]);
    await page.locator('#save').click();await page.waitForFunction(()=>document.querySelector('#count').textContent==='(2/8)');
    assert.deepEqual(profiles.map(p=>p.name),['A','B']);
    await page.locator('#profiles .card').first().getByRole('button',{name:'Chọn, chưa kết nối',exact:true}).click();
    await page.waitForFunction(()=>document.querySelector('#state').textContent.includes('Đã ngắt'));
    mode='slow';await page.locator('#retry').click();await page.locator('#router').click();
    await page.waitForFunction(()=>document.querySelector('#state').textContent.includes('Internet trực tiếp'));
    assert.equal(maximum,1,'Polling and mutations must not overlap on the ESP HTTP server');
    mode='auth';await page.goto(url);await page.waitForFunction(()=>document.querySelector('#state').textContent==='Cần đăng nhập quản trị');
    assert((await page.locator('#notice').textContent()).includes('đăng nhập'));
    mode='bad_json';await page.goto(url);await page.waitForFunction(()=>document.querySelector('#state').textContent==='Không đọc được trạng thái VPN');
    mode='hang_get';await page.goto(url);await page.waitForFunction(()=>document.querySelector('#notice').textContent.includes('8 giây'),null,{timeout:11000});
    assert.notEqual(await page.locator('#state').textContent(),'Đang tải…');
    mode='ok';await page.locator('#retry').click();await page.waitForFunction(()=>document.querySelector('#state').textContent.includes('Internet trực tiếp'));
    mode='hang_post';await page.locator('#router').click();await page.waitForFunction(()=>!document.querySelector('#router').disabled,null,{timeout:11000});
    assert((await page.locator('#notice').textContent()).includes('chưa xác nhận'));
    assert.deepEqual(errors,[]);
    mode='script_error';await page.goto(url);await page.waitForFunction(()=>document.querySelector('#state').textContent==='Không khởi chạy được giao diện VPN');
    console.log('WebKit UI tests passed: mobile layout, auth, two-file import without File.text, serialized requests, invalid JSON, GET/POST timeouts, retry, startup-error fallback.');
  } finally {await browser.close();for(const s of sockets)s.destroy();server.close();}
})().catch(e=>{console.error(e);process.exitCode=1;for(const s of sockets)s.destroy();server.close();});
