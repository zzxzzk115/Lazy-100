# LAZY-100 API 速查表

控制台暴露给卡带的全部函数,按功能分块。可选参数用 `[方括号]` 标注。本表与编辑器内速查
(代码编辑器的书本图标)以及 `source/lazy100/script/api_doc.cpp` 中的注册表一一对应——请
同步更新。

English version: [../CHEATSHEET.md](../CHEATSHEET.md)

## 生命周期回调

```lua
_init()                                    -- 卡带启动时调用一次
_update()                                  -- 30 fps 的游戏逻辑
_update60()                                -- 定义它(替代 _update)即为 60 fps
_draw()                                    -- 每帧渲染(在 update 之后)
```

## 图形

```lua
cls([color])                               -- 清屏(默认颜色 0)
pset(x, y, [color])                        -- 画一个像素
pget(x, y)                                 -- 读取像素的调色板索引
line(x0, y0, x1, y1, [color])              -- 两点之间的直线
rect(x0, y0, x1, y1, [color])              -- 矩形描边
rectfill(x0, y0, x1, y1, [color])          -- 实心矩形
circ(x, y, radius, [color])                -- 圆形描边
circfill(x, y, radius, [color])            -- 实心圆
oval(x0, y0, x1, y1, [color])              -- 外接框内的椭圆描边
ovalfill(x0, y0, x1, y1, [color])          -- 外接框内的实心椭圆
print(text, [x], [y], [color])             -- 绘制文本;返回结束处的 x
camera([x], [y])                           -- 所有绘制的滚动偏移;无参数则复位
clip([x], [y], [w], [h])                   -- 限制绘制到矩形区域;无参数则复位
fillp([pattern], [color2])                 -- 形状类图元的 4x4 抖动图案;为 1 的位画 color2
                                           -- (缺省则跳过/透明);无参数复位
```

`fillp` 作用于 `pset`、`line`、`rect(fill)`、`circ(fill)`、`oval(fill)`,不作用于精灵和
`print`。图案为 16 位,bit 15 = 4x4 单元的左上角
(例如 `fillp(0b0101101001011010)` 是棋盘格)。

## 精灵

```lua
spr(n, x, y, [w], [h], [flip_x], [flip_y]) -- 绘制精灵 n(w*h 个 16px 单元的块)
sspr(sx, sy, sw, sh, dx, dy, [dw], [dh], [flip_x], [flip_y])
                                           -- 绘制精灵表矩形,缩放到 dw*dh
sget(x, y)                                 -- 读取精灵表像素
sset(x, y, [color])                        -- 写入精灵表像素
fget(n, [bit])                             -- 精灵标志(或单个标志位)
fset(n, flags_or_bit, [on])                -- 设置精灵标志(或单个标志位)
```

## 地图

```lua
mget(cel_x, cel_y)                         -- 地图格子上的图块(255 = 空)
mset(cel_x, cel_y, tile)                   -- 写入地图格子
map([cel_x], [cel_y], [scr_x], [scr_y], [cel_w], [cel_h])
                                           -- 以 16px 图块绘制一片地图区域
```

## 调色板

```lua
pal([c0], [c1], [mode])                    -- 颜色 c0 重映射为 c1(mode 1 = 屏幕级);无参数复位
palt([color], [transparent])               -- 设置某颜色的透明性;无参数复位
```

## 音频

```lua
sfx(n, [channel], [offset], [length])      -- 从第 offset 个音符起播放 length 个音符
                                           -- n = -1 停止该声道,-2 释放其循环
music([n])                                 -- 从模式 n 播放音乐;music(-1) 停止
```

每条音效模式还带有**速度**、可选的**循环区间**(在 loop start/end 之间反复,直到停止或
释放),以及逐音符**效果器**:1 滑音、2 颤音、3 下坠、4 淡入、5 淡出、6/7 快/慢琶音。
音乐模式在音乐编辑器中可设 循环起点 / 循环终点 / 停止 旗标。

## 输入

```lua
btn([b], [player])                         -- 按键是否按住?无参数返回位掩码
btnp([b], [player])                        -- 按键是否刚按下(带自动重复)?
stat(n)                                    -- 控制台状态:32/33 鼠标 x/y,34 按键位
```

## 数学

角度单位是**圈**:`1.0` 等于一整圈,y 轴向下(`sin` 取反)。

```lua
flr(x)                                     -- 向下取整
ceil(x)                                    -- 向上取整
abs(x)                                     -- 绝对值
min(a, b)                                  -- 两者取小
max(a, b)                                  -- 两者取大
mid(a, b, c)                               -- 三者取中(夹取)
sgn(x)                                     -- -1 或 1(0 视为正)
sqrt(x)                                    -- 平方根
sin(turns)                                 -- 正弦;角度为圈数,y 取反
cos(turns)                                 -- 余弦;角度为圈数
atan2(dx, dy)                              -- 向量的角度,圈数 0..1
rnd([max_or_table])                        -- [0,max) 随机数,或表中的随机元素
srand([seed])                              -- 设置随机数种子
```

## 时间

```lua
t()                                        -- 卡带启动以来的秒数
time()                                     -- t() 的别名
```

## 存档

```lua
cartdata(id)                               -- 打开卡带的 64 槽存档文件(saves/<id>)
dset(index, value)                         -- 向 0..63 号槽存一个数(持久化)
dget(index)                                -- 读取存档槽(未设置为 0)
```

## 表

```lua
add(t, value, [index])                     -- 追加(或插入)到表;返回 value
del(t, value)                              -- 删除第一个匹配的值
deli(t, [index])                           -- 按索引删除(默认最后一个)
count(t, [value])                          -- #t,或等于 value 的元素个数
all(t)                                     -- 迭代器:for item in all(t) do ... end
foreach(t, fn)                             -- 对每个元素调用 fn(item)
```

## 字符串

```lua
tostr(value, [hex])                        -- 值 -> 字符串(hex 以十六进制格式化数字)
tonum(s)                                   -- 字符串 -> 数字,失败为 nil
chr(code, ...)                             -- 由字节码得到字符
ord(s, [index])                            -- 字符的字节码
sub(s, from, [to])                         -- 子串(1 起始,含端点)
split(s, [sep], [to_number])               -- 切分为表(默认分隔符 ",")
```

## 协程

```lua
cocreate(fn)                               -- 创建协程
coresume(co, ...)                          -- 运行/继续;返回 ok, ...
costatus(co)                               -- "suspended"、"running" 或 "dead"
yield(...)                                 -- 暂停协程,把值交回去
```
