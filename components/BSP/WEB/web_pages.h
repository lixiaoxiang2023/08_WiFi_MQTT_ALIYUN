#pragma once

static const char *wifi_config_html =
"<!DOCTYPE html>"
"<html lang='zh-CN'>"
"<head>"
"<meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
"<title>ESP32 设备管理控制台</title>"

"<style>"
"body{margin:0;font-family:Arial;background:#f2f4f8;}"
".header{background:#2d8cf0;color:#fff;padding:14px;text-align:center;font-size:18px;}"
".tabs{display:flex;background:#fff;border-bottom:1px solid #ddd;}"
".tab{flex:1;text-align:center;padding:12px 0;cursor:pointer;color:#555;}"
".tab.active{color:#2d8cf0;border-bottom:3px solid #2d8cf0;font-weight:bold;}"
".content{padding:16px;max-width:820px;margin:auto;}"
".page{display:none;}"
".page.active{display:block;}"
".card{background:#fff;border-radius:8px;padding:16px;margin-bottom:16px;box-shadow:0 2px 6px rgba(0,0,0,0.08);}"
".card h3{margin:0 0 10px;font-size:16px;border-left:4px solid #2d8cf0;padding-left:8px;}"
"label{display:block;margin-top:12px;font-size:14px;font-weight:500;}"
"input,select{width:100%;padding:8px;margin-top:6px;border-radius:4px;border:1px solid #ccc;font-size:14px;}"
".tip{font-size:12px;color:#888;margin-top:4px;line-height:1.5;}"
".btn{margin-top:14px;padding:8px 16px;border:none;border-radius:4px;font-size:14px;cursor:pointer;}"
".btn-primary{background:#2d8cf0;color:#fff;}"
".btn-success{background:#19be6b;color:#fff;}"
".inline{display:flex;gap:12px;flex-wrap:wrap;align-items:end;}"
".inline>div{flex:1;min-width:220px;}"
"</style>"

"</head>"
"<body>"

"<div class='header'>ESP32 设备管理控制台</div>"

"<div class='tabs'>"
"<div class='tab active' onclick='showPage(0)'>系统配置</div>"
"<div class='tab' onclick='showPage(1)'>仪器配置</div>"
"</div>"

"<div class='content'>"

/* ================= 系统页 ================= */
"<div class='page active'>"

"<div class='card'>"
"<h3>WiFi 设置</h3>"
"<button class='btn btn-primary' onclick='scanWifi()'>扫描附近 WiFi</button>"
"<label>WiFi 网络</label>"
"<select id='wifiList'></select>"
"<label>WiFi 密码</label>"
"<input type='password' id='wifiPwd'>"
"<button class='btn btn-success' onclick='connectWifi()'>保存并连接</button>"
"</div>"

"<div class='card'>"
"<h3>设备信息</h3>"
"<p>软件版本号：V1.0.0</p>"
"<p>硬件版本号：HW-A1</p>"
"</div>"

"</div>"

/* ================= 仪器页 ================= */
"<div class='page'>"

"<div class='card'>"
"<h3>产品线选择</h3>"
"<label>产品线</label>"
"<select id='productLine' onchange='updateUI()'>"
"<option selected>半自动</option>"
"<option>全自动</option>"
"<option>妇科</option>"
"<option>动物</option>"
"<option>自定义</option>"
"</select>"

"<div id='modelBox'>"
"<label>机型</label>"
"<select id='model'></select>"
"</div>"
"</div>"

"<div class='card'>"
"<h3>固件与数据配置</h3>"

"<label>下载固件版本号</label>"
"<input placeholder='如：1.00.01'>"
"<div class='tip'>示例格式：1.00.01</div>"

"<h3 style='margin-top:18px'>数据上传配置</h3>"

"<label>本地文件名称</label>"

"<div class='inline'>"
"<div>"
"<select id='localFileSelect'></select>"
"</div>"
"<div style='flex:0'>"
"<button class='btn btn-primary' onclick='loadUsbFiles()'>刷新文件列表</button>"
"</div>"
"</div>"

"<div class='tip'>显示U盘中的文件列表</div>"

"<label>上传文件名称</label>"
"<input id='uploadServerInput' placeholder='如：upload_20240208.dat'>"

"<button class='btn btn-success' onclick='submitDataConfig()'>保存配置</button>"

"</div>"
"</div>"

"</div>"

/* ================= JS ================= */

"<script>"

/* 页面切换 */
"function showPage(i){"
"document.querySelectorAll('.tab').forEach(t=>t.classList.remove('active'));"
"document.querySelectorAll('.page').forEach(p=>p.classList.remove('active'));"
"document.querySelectorAll('.tab')[i].classList.add('active');"
"document.querySelectorAll('.page')[i].classList.add('active');"
"}"

/* 产品线联动 */
"function updateUI(){"
"const line=document.getElementById('productLine').value;"
"const model=document.getElementById('model');"
"model.innerHTML='';"
"const map={'半自动':['UC-50A','UC-50BC','UC-280A','UC-280B'],"
"'全自动':['UC-1600','UC-1800'],"
"'妇科':['SL-1000'],"
"'动物':['EM-100']};"
"if(map[line]){"
"map[line].forEach(m=>{"
"let o=document.createElement('option');"
"o.text=m;"
"model.add(o);"
"});"
"}"
"}"
"updateUI();"

/* ======== 重点：U盘文件刷新（完全稳定版）======== */
"async function loadUsbFiles(){"
"try{"
"const response=await fetch('/usb_files?t='+Date.now());"
"if(!response.ok) throw new Error('HTTP error');"
"const data=await response.json();"

"const select=document.getElementById('localFileSelect');"
"select.innerHTML='';"

"if(!data || data.length===0){"
"let o=document.createElement('option');"
"o.text='未发现文件';"
"select.add(o);"
"return;"
"}"

"data.forEach(file=>{"
"let o=document.createElement('option');"
"o.text=file;"
"o.value=file;"
"select.add(o);"
"});"

"}catch(err){"
"alert('读取U盘失败');"
"}"
"}"

/* 页面加载自动刷新 */
/* 页面加载自动读取NVS配置并恢复 */
"window.onload = async function(){"
/* ===== 1️⃣ 读取已连接WiFi信息 ===== */
"try{"
"const wifiResp = await fetch('/get_wifi_info?t=' + Date.now());"
"if(wifiResp.ok){"
"const wifiData = await wifiResp.json();"

"if(wifiData.ssid){"
"document.getElementById('wifiPwd').value = wifiData.password || '';"

"let wifiSelect = document.getElementById('wifiList');"
"let exists = false;"
"for(let i=0;i<wifiSelect.options.length;i++){"
"if(wifiSelect.options[i].value === wifiData.ssid){"
"exists = true;"
"break;"
"}"
"}"

"if(!exists){"
"let o = document.createElement('option');"
"o.text = wifiData.ssid + ' (已连接)';"
"o.value = wifiData.ssid;"
"wifiSelect.add(o);"
"}"

"wifiSelect.value = wifiData.ssid;"
"}"
"}"
"}catch(e){"
"console.warn('读取WiFi信息失败');"
"}"

/* ===== 2️⃣ 读取NVS数据配置 ===== */
"try{"
"const resp = await fetch('/get_config?t=' + Date.now());"
"if(resp.ok){"
"const cfg = await resp.json();"

"const localSelect = document.getElementById('localFileSelect');"
"const uploadInput = document.getElementById('uploadServerInput');"

"const localFile = cfg.localFile || '';"
"uploadInput.value = cfg.uploadServer || '';"

"await loadUsbFiles();"

"let optionExists = false;"
"for(let i=0;i<localSelect.options.length;i++){"
"if(localSelect.options[i].value === localFile){"
"optionExists = true;"
"break;"
"}"
"}"

"if(optionExists){"
"localSelect.value = localFile;"
"}else if(localFile){"
"let o = document.createElement('option');"
"o.text = localFile + ' (已保存)';"
"o.value = localFile;"
"localSelect.add(o);"
"localSelect.value = localFile;"
"}"

"}else{"
"await loadUsbFiles();"
"}"
"}catch(e){"
"console.warn('获取配置失败');"
"await loadUsbFiles();"
"}"

"};"

/* WiFi 扫描 */
"function scanWifi(){"
"fetch('/scan?t='+Date.now())"
".then(response=>response.json())"
".then(data=>{"
"const select=document.getElementById('wifiList');"
"select.innerHTML='';"
"data.forEach(item=>{"
"let o=document.createElement('option');"
"o.text=item.ssid+' ('+item.rssi+' dBm)';"
"o.value=item.ssid;"
"select.add(o);"
"});"
"})"
".catch(()=>alert('扫描失败'));"
"}"

/* WiFi 连接 */
"function connectWifi(){"
"const ssid=document.getElementById('wifiList').value;"
"const pwd=document.getElementById('wifiPwd').value;"
"if(!ssid){alert('请选择WiFi');return;}"
"fetch('/connect_wifi',{"
"method:'POST',"
"headers:{'Content-Type':'application/json'},"
"body:JSON.stringify({ssid:ssid,password:pwd})"
"})"
".then(resp=>resp.text())"
".then(data=>alert(data))"
".catch(()=>alert('连接失败'));"
"}"

/* 保存配置 */
"function submitDataConfig(){"
"const localFile=document.getElementById('localFileSelect').value;"
"const uploadServer=document.getElementById('uploadServerInput').value.trim();"
"fetch('/save_config',{"
"method:'POST',"
"headers:{'Content-Type':'application/json'},"
"body:JSON.stringify({localFile:localFile,uploadServer:uploadServer})"
"})"
".then(resp=>resp.text())"
".then(()=>alert('配置已提交'))"
".catch(()=>alert('提交失败'));"
"}"

"</script>"
"</body></html>";