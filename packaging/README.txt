XHL Remote Chest PinYin
=======================

为「远程魔法箱」的搜索框加入拼音搜索与中文输入法支持。

使用
----
在远程魔法箱的搜索框中：

  nvyad       全拼搜索（如：充盈女妖凝胶）
  ygh         首字母搜索（如：远古核心）
  #fashu      按分类/类型筛选（#法术、#Cailiao 等写法也可以）
  中文        直接用系统输入法输入，组字会显示在搜索框里

安装
----
将整个文件夹放入 XHL 加载器的mod目录，保持 mod.json、
native-plugin.ini 与 XHL-Remote-Chest-PinYin.dll 在同一目录。
启动游戏后插件自动加载，无需额外操作。

配置
----
pin_yin.ini

  [Debug]
  Hooks=1    1=搜索框激活时屏蔽游戏按键，0=关闭
  IME=1      1=接管系统输入法，0=关闭

修改后重启游戏或重新加载插件生效。

日志
----
游戏 exe 所在目录下的 XHL-Remote-Chest-PinYin.log 记录补丁与输入法状态。
反馈问题时请附上该文件。

卸载
----
删除本文件夹即可。插件停止时会自动还原所有内存补丁。

许可证
------
MIT，详见 LICENSE.txt。
