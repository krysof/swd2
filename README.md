# 轩辕剑2（SWD2）可移植重写

《轩辕剑2》的单进程、跨平台 C++20 重写。原版多个 DOS 可执行文件的行为已合并到
`swd2_rewrite`；平台无关游戏核心与 SDL 外壳分离，可构建为桌面程序或 WebAssembly。

原始游戏数据放在本地 `game/`，不会进入源码 Git 历史。详细的逆向状态、模块关系和
原生构建方法见 [`src/README.md`](src/README.md) 与
[`src/architecture.md`](src/architecture.md)。

运行时不包含、也不会调用 DOSBox：MEO、RPG、FIG、DEMO 与原启动器流程已合并进同一个
C++ 程序。逆向记录中提到 DOSBox-X，只表示它被用作原版画面/音频的离线差分基准。

项目的100%硬性验收规则见 [`COMPLETION.md`](COMPLETION.md)。在
`./scripts/audit-completion.py --require-complete` 成功前，任何构建或发布都只是阶段版本。

## WebAssembly

在线构建：<https://krysof.github.io/swd2/>

安装 Emscripten、CMake 与 Ninja 后运行：

```sh
./scripts/build-wasm.sh
python3 -m http.server 8000 --directory build-wasm/site
```

构建脚本会自动调用 `scripts/verify-wasm.sh`，检查四个发布文件、WASM magic/version、
JS 对 `.wasm/.data` 的引用，并在可用时通过 Binaryen 重新解析模块。

然后访问 <http://localhost:8000/>。浏览器产物为 `index.html`、`index.js`、
`index.wasm` 与 `index.data`，资源使用相对 URL，既可部署在域名根目录，也可部署到
GitHub Pages 的 `/swd2/` 子路径。存档挂载在 IDBFS 中，保留于当前浏览器；系统菜单
每次写入后会立即执行异步 `FS.syncfs(false)`，不必等到游戏进程退出才落盘。

移动端首屏会显示“白河愁 破解移植”、`http://www.ff18.com` 与声音启动按钮；点击时在
同一用户手势内预建 SDL 将复用的 Web Audio context，并申请横屏和 Screen Wake Lock。
竖持手机若不支持浏览器方向锁，CSS 会把游戏画面及六个触摸按钮整体旋转为横屏布局；
按钮禁止文字选择/长按菜单，方向键支持长按，ESC/回车保持单次触发。页面从后台返回时
会重新申请常亮。Wake Lock 只能阻止浏览器可见期间的自动休眠，不能绕过用户手动锁屏、
系统强制省电或浏览器终止后台页面。

只发布生成后的静态站点、不推送源码：

```sh
./scripts/deploy-pages.sh krysof/swd2
```
