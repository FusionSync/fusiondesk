# GitHub Pages 文档发布

本仓库使用 MkDocs + Material for MkDocs 构建文档站，并通过 GitHub Actions 发布到 GitHub Pages。

## 本地预览

安装文档依赖：

```bash
python -m pip install -r requirements-docs.txt
```

本地启动：

```bash
mkdocs serve
```

构建检查：

```bash
mkdocs build --strict
```

## GitHub Pages 设置

在 GitHub 仓库中打开：

```text
Settings -> Pages -> Build and deployment
```

将 Source 设置为：

```text
GitHub Actions
```

之后推送到 `main` 或 `master` 时，`.github/workflows/docs.yml` 会自动构建并部署文档站。

## 发布流程

```text
push / workflow_dispatch
  -> checkout
  -> setup python
  -> pip install requirements-docs.txt
  -> mkdocs build --strict
  -> upload Pages artifact
  -> deploy to GitHub Pages
```

Pull Request 只执行构建检查，不部署。

## 文档结构

```text
mkdocs.yml
requirements-docs.txt
docs/
  index.md
  getting-started.md
  usage/
  features/
  development/
  deployment/
  architecture/
```

`docs/architecture/` 是现有架构文档的源目录。新增使用文档应优先放在 `docs/usage/`、`docs/features/`、`docs/development/` 或 `docs/deployment/`。
