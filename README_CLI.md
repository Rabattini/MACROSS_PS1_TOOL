# LZSS CLI (PS1-compatible)

Perfis:
- **Rápido**:    bucket_limit=64,  max_candidates=128
- **Equilibrado**: bucket_limit=128, max_candidates=256  *(padrão)*
- **Máxima compressão**: bucket_limit=256, max_candidates=1024
- **Lazy Matching**: ligado por padrão, desative com `--no-lazy`

Uso:
```
lzss_cli compress   arquivo.bin [-o saida.lzss] [-p rapido|equilibrado|maximo] [--no-lazy]
lzss_cli decompress arquivo.lzss [-o saida.decomp.bin] [--out-len N]
```
