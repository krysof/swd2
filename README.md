# SWD2 portable rewrite

《水浒传 2》的单进程、跨平台 C++20 重写。原版多个 DOS 可执行文件的行为已合并到
`swd2_rewrite`；平台无关游戏核心与 SDL 外壳分离，可构建为桌面程序或 WebAssembly。

原始游戏数据放在本地 `game/`，不会进入源码 Git 历史。详细的逆向状态、模块关系和
原生构建方法见 [`src/README.md`](src/README.md) 与
[`src/architecture.md`](src/architecture.md)。

## WebAssembly

安装 Emscripten、CMake 与 Ninja 后运行：

```sh
./scripts/build-wasm.sh
python3 -m http.server 8000 --directory build-wasm/site
```

然后访问 <http://localhost:8000/>。浏览器产物为 `index.html`、`index.js`、
`index.wasm` 与 `index.data`，资源使用相对 URL，既可部署在域名根目录，也可部署到
GitHub Pages 的 `/swd2/` 子路径。存档挂载在 IDBFS 中，保留于当前浏览器；系统菜单
每次写入后会立即执行异步 `FS.syncfs(false)`，不必等到游戏进程退出才落盘。

只发布生成后的静态站点、不推送源码：

```sh
./scripts/deploy-pages.sh krysof/swd2
```
