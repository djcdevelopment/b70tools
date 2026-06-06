# Local inference runbook - Ollama on the second B70

How to bring up the local Mistral model used by Ollama-backed runs. The goal is to keep
the model blobs on D: and pin Ollama to the second Arc Pro B70 so the primary card stays
free.

## Launch Ollama pinned to the second B70 (32 GB VRAM)

```powershell
$env:OLLAMA_MODELS = "D:\work\models"      # where the model blobs live
$env:GGML_VK_VISIBLE_DEVICES = "1"         # Vulkan backend, device 1 = the second B70
ollama serve
# then, in another shell (or after serve is up):
ollama run mistral-small-24b-64k-b70
```

## Why these settings

- `GGML_VK_VISIBLE_DEVICES=1` keeps Ollama on Vulkan and binds it to the second card.
- `OLLAMA_MODELS=D:\work\models` keeps the model blobs on D: instead of the default
  profile location.
- `mistral-small-24b-64k-b70` is Mistral Small 24B with a 64k context window, which
  loads reliably on the 32 GB card. Verified 2026-06-03.

## Sanity checks

```powershell
curl http://localhost:11434/api/tags
```
