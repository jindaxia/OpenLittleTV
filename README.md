# OpenLittleTV

南湘小隐的仿B站外形Oled小电视代码

重写了全部代码，目前只完成设置部分，设置改成写json文件，可选文件系统LittleFS，SPIFFS，SDFS，建议使用LittleFS

# 开发计划
- [ ] 增加触摸功能或功能按键以实现更多的操作
- [ ] 番茄钟
- [ ] 正向天数计时（情侣时间等）
- [ ] 天气动画-不同的天气播放不同的动画表情
- [ ] 声音播放功能-可用番茄钟、闹钟、粉丝数量增加提醒或者其他语音
- [ ] B站粉丝数量变化提醒（动画或声音提醒）
- [ ] 同意局域网的两个小电视交互，动画交互等
- [ ] 时间流动，为了明显的感觉到时间流逝，做出让时间数字不停的流逝的感觉
- [ ] 模拟时钟界面
- [ ] BTC报价器
- [ ] 音乐频谱

## 已经实现的功能
- [x] 天气显示，可显示天气，温湿度，风速，空气质量等参数
- [x] 时钟显示
- [x] 天数倒计时
- [x] B站粉丝监控
- [x] 电脑性能监控，显示一些电脑运行参数
- [x] 血糖水平显示-基于Nightscout


## 环境

ESP8266 Arduino SDK 2.7.4

ArduinoJson  6.18.0

