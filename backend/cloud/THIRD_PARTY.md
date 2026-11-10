# 云后端第三方包清单

日期：2026-11-09。版本与发行包 SHA-256 以 `uv.lock` 为准。以下是直接和传递依赖，不包含桌面应用依赖。

运行环境必须保留各包 `.dist-info` 中的许可证、NOTICE 和作者文件。表内路径相对于 Python site-packages；本轮没有修改或再发布依赖源码。Python 标准库另遵守 PSF 许可，SQLite 为公有领域；生产镜像的系统依赖须在部署阶段另行登记。

| 包 | 版本 | 用途 | 许可证 | 已核对的许可文件 |
| --- | --- | --- | --- | --- |
| [argon2-cffi](https://pypi.org/project/argon2-cffi/25.1.0/) | 25.1.0 | 密码 Argon2id | MIT | `licenses/argon2-cffi/LICENSE` |
| [argon2-cffi-bindings](https://pypi.org/project/argon2-cffi-bindings/26.1.0/) | 26.1.0 | Argon2 C 绑定 | MIT | `licenses/argon2-cffi-bindings/LICENSE` |
| Argon2 reference implementation（bindings sdist 内附） | 随 26.1.0 锁定源码 | 哈希实现 | CC0-1.0 OR Apache-2.0，本项目选择 CC0-1.0 | `licenses/argon2/LICENSE`（保留两份许可全文） |
| [cffi](https://pypi.org/project/cffi/2.1.1/) | 2.1.1 | C 绑定 | MIT-0 | `licenses/cffi/LICENSE` |
| [pycparser](https://pypi.org/project/pycparser/3.12/) | 3.12 | CFFI 依赖 | BSD-3-Clause | `licenses/pycparser/LICENSE` |
| [annotated-doc](https://pypi.org/project/annotated-doc/0.0.5/) | 0.0.5 | 运行 | MIT | `annotated_doc-0.0.5.dist-info/licenses/LICENSE` |
| [annotated-types](https://pypi.org/project/annotated-types/0.8.0/) | 0.8.0 | 运行 | MIT | `annotated_types-0.8.0.dist-info/licenses/LICENSE` |
| [anyio](https://pypi.org/project/anyio/4.16.0/) | 4.16.0 | 运行 | MIT | `anyio-4.16.0.dist-info/licenses/LICENSE` |
| [certifi](https://pypi.org/project/certifi/2026.10.23/) | 2026.10.23 | 测试/开发 | MPL-2.0 | `certifi-2026.10.23.dist-info/licenses/LICENSE` |
| [click](https://pypi.org/project/click/8.5.1/) | 8.5.1 | 运行 | BSD-3-Clause | `click-8.5.1.dist-info/licenses/LICENSE.txt` |
| [colorama](https://pypi.org/project/colorama/0.4.6/) | 0.4.6 | 测试/开发 | BSD-3-Clause | Windows 条件依赖；许可见官方发行页，当前 macOS 环境未安装 |
| [fastapi](https://pypi.org/project/fastapi/0.162.2/) | 0.162.2 | 运行 | MIT | `fastapi-0.162.2.dist-info/licenses/LICENSE` |
| [h11](https://pypi.org/project/h11/0.16.0/) | 0.16.0 | 运行 | MIT | `h11-0.16.0.dist-info/licenses/LICENSE.txt` |
| [httpcore](https://pypi.org/project/httpcore/1.0.9/) | 1.0.9 | 测试/开发 | BSD-3-Clause | `httpcore-1.0.9.dist-info/licenses/LICENSE.md` |
| [httpx](https://pypi.org/project/httpx/0.28.1/) | 0.28.1 | 测试/开发 | BSD-3-Clause | `httpx-0.28.1.dist-info/licenses/LICENSE.md` |
| [idna](https://pypi.org/project/idna/3.20/) | 3.20 | 运行 | BSD-3-Clause | `idna-3.20.dist-info/licenses/LICENSE.md` |
| [iniconfig](https://pypi.org/project/iniconfig/2.3.1/) | 2.3.1 | 测试/开发 | MIT | `iniconfig-2.3.1.dist-info/licenses/LICENSE` |
| [itsdangerous](https://pypi.org/project/itsdangerous/2.2.0/) | 2.2.0 | 运行 | BSD-3-Clause | `itsdangerous-2.2.0.dist-info/LICENSE.txt` |
| [opentelemetry-api](https://pypi.org/project/opentelemetry-api/1.45.2/) | 1.45.2 | 运行 | Apache-2.0 | `opentelemetry_api-1.45.2.dist-info/licenses/LICENSE` |
| [packaging](https://pypi.org/project/packaging/26.3/) | 26.3 | 测试/开发 | Apache-2.0 OR BSD-2-Clause | `packaging-26.3.dist-info/licenses/LICENSE`<br>`packaging-26.3.dist-info/licenses/LICENSE.APACHE`<br>`packaging-26.3.dist-info/licenses/LICENSE.BSD` |
| [pluggy](https://pypi.org/project/pluggy/1.7.0/) | 1.7.0 | 测试/开发 | MIT | `pluggy-1.7.0.dist-info/licenses/LICENSE` |
| [pydantic](https://pypi.org/project/pydantic/2.14.3/) | 2.14.3 | 运行 | MIT | `pydantic-2.14.3.dist-info/licenses/LICENSE` |
| [pydantic-core](https://pypi.org/project/pydantic-core/2.50.4/) | 2.50.4 | 运行 | MIT | `pydantic_core-2.50.4.dist-info/licenses/LICENSE` |
| [pygments](https://pypi.org/project/pygments/2.21.0/) | 2.21.0 | 测试/开发 | BSD-2-Clause | `pygments-2.21.0.dist-info/licenses/AUTHORS`<br>`pygments-2.21.0.dist-info/licenses/LICENSE` |
| [pytest](https://pypi.org/project/pytest/9.2.0/) | 9.2.0 | 测试/开发 | MIT | `pytest-9.2.0.dist-info/licenses/LICENSE` |
| [starlette](https://pypi.org/project/starlette/1.10.0/) | 1.10.0 | 运行 | BSD-3-Clause | `starlette-1.10.0.dist-info/licenses/LICENSE.md` |
| [typing-extensions](https://pypi.org/project/typing-extensions/4.16.0/) | 4.16.0 | 运行 | PSF-2.0 | `typing_extensions-4.16.0.dist-info/licenses/LICENSE` |
| [typing-inspection](https://pypi.org/project/typing-inspection/0.4.5/) | 0.4.5 | 运行 | MIT | `typing_inspection-0.4.5.dist-info/licenses/LICENSE` |
| [uvicorn](https://pypi.org/project/uvicorn/0.54.0/) | 0.54.0 | 运行 | BSD-3-Clause | `uvicorn-0.54.0.dist-info/licenses/LICENSE.md` |

测试依赖中的 certifi 使用 MPL-2.0；保留发行包许可与来源，未修改其文件。packaging 的许可选择为 BSD-2-Clause（发行包同时携带 Apache-2.0）；所有许可文件仍保留。

安装工具本轮使用 uv 0.13.1，不作为服务运行依赖。依赖比较、线程/资源边界和未完成的平台验证见[选型记录](../../docs/superpowers/evidence/2026-11-09-cloud-backend-dependencies.md)。
