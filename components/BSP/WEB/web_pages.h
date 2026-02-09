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
".card{background:#fff;border-radius:8px;padding:16px;margin-bottom:16px;"
"box-shadow:0 2px 6px rgba(0,0,0,0.08);}"
".card h3{margin:0 0 10px;font-size:16px;border-left:4px solid #2d8cf0;padding-left:8px;}"
"label{display:block;margin-top:12px;font-size:14px;font-weight:500;}"
"input,select{width:100%;padding:8px;margin-top:6px;border-radius:4px;border:1px solid #ccc;font-size:14px;}"
".tip{font-size:12px;color:#888;margin-top:4px;line-height:1.5;}"
".btn{margin-top:14px;padding:8px 16px;border:none;border-radius:4px;font-size:14px;cursor:pointer;}"
".btn-primary{background:#2d8cf0;color:#fff;}"
".btn-success{background:#19be6b;color:#fff;}"
".inline{display:flex;gap:12px;flex-wrap:wrap;}"
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

/* 系统配置 */
"<div class='page active'>"
"<div class='card'>"
"<h3>WiFi 设置</h3>"
"<button class='btn btn-primary'>扫描附近 WiFi</button>"
"<label>WiFi 网络</label>"
"<select><option>请选择 WiFi</option></select>"
"<label>WiFi 密码</label>"
"<input type='password'>"
"<button class='btn btn-success'>保存并连接</button>"
"</div>"

"<div class='card'>"
"<h3>设备信息</h3>"
"<p>仪器软件版本号：V1.0.0</p>"
"<p>仪器硬件版本号：HW-A1</p>"
"</div>"
"</div>"

/* 仪器配置 */
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

/* 标准产品 */
"<div class='card' id='normalConfig'>"
"<h3>固件与数据配置</h3>"

"<label>下载固件版本号</label>"
"<input placeholder='如：1.00.01'>"
"<div class='tip'>示例格式：1.00.01</div>"

"<h3 style='margin-top:18px'>数据上传配置</h3>"

"<div class='inline'>"
"<div>"
"<label>本地数据文件</label>"
"<select>"
"<option>请选择本地数据文件</option>"
"<option>result_20240208.dat</option>"
"<option>result_20240207.dat</option>"
"<option>log_20240208.txt</option>"
"</select>"
"<div class='tip'>设备当前可上传的数据文件列表</div>"
"</div>"

"<div>"
"<label>数据上传服务器名称</label>"
"<input placeholder='如：upload_20240208.dat'>"
"</div>"
"</div>"
"</div>"

/* 自定义产品 */
"<div class='card' id='customConfig' style='display:none'>"
"<h3>自定义产品配置</h3>"
"<div class='tip'>用于非标准机型或调试阶段</div>"

"<label>服务器固件名称</label>"
"<input>"

"<label>本地固件名称</label>"
"<input>"

"<label>本地数据文件</label>"
"<select>"
"<option>请选择本地数据文件</option>"
"<option>custom_data_01.dat</option>"
"<option>custom_data_02.dat</option>"
"</select>"

"<label>数据上传服务器名称</label>"
"<input>"
"</div>"

"</div>"

"<script>"
"function showPage(i){"
"document.querySelectorAll('.tab').forEach(t=>t.classList.remove('active'));"
"document.querySelectorAll('.page').forEach(p=>p.classList.remove('active'));"
"document.querySelectorAll('.tab')[i].classList.add('active');"
"document.querySelectorAll('.page')[i].classList.add('active');"
"}"

"function updateUI(){"
"const line=document.getElementById('productLine').value;"
"const modelBox=document.getElementById('modelBox');"
"const model=document.getElementById('model');"
"const normal=document.getElementById('normalConfig');"
"const custom=document.getElementById('customConfig');"

"model.innerHTML='';"

"if(line==='自定义'){"
"modelBox.style.display='none';"
"normal.style.display='none';"
"custom.style.display='block';"
"return;"
"}"

"modelBox.style.display='block';"
"normal.style.display='block';"
"custom.style.display='none';"

"const map={"
"'半自动':['UC-50A','UC-50BC','UC-280A','UC-280B'],"
"'全自动':['UC-1600','UC-1800'],"
"'妇科':['SL-1000'],"
"'动物':['EM-100']"
"};"

"map[line].forEach(m=>{"
"const o=document.createElement('option');"
"o.text=m;"
"model.add(o);"
"});"
"}"
"updateUI();"
"</script>"

"</body></html>";
