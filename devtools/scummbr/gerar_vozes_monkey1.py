#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Gera a tabela de vozes do Monkey Island 1 traduzido (engines/scumm/monkey1_vozes.inc).

A traducao do Monkey Island 1 foi feita sobre a Ultimate Talkie Edition (UTE). No
texto da UTE, cada fala tem um "endereco" (offset) que o jogo usa para achar o
audio no monkey.sog. O ScummVMBR nao leva o monkey.sog: ele toca a fala do
Speech.xwb da Special Edition do proprio jogador (a UTE foi feita a partir
desse mesmo audio). Esta tabela diz, para cada endereco, qual fala do
Speech.xwb tocar. Ela guarda so numeros e nomes, nenhum audio.

Como a tabela e feita: o monkey.sog e o Speech.xwb tem o mesmo audio (o sog e o
xwb recodificado), entao cada fala do sog e ligada a fala do xwb que tem a mesma
duracao, em amostras, e a maior correlacao de forma de onda.

Falas que a UTE montou emendando outras (precos do Stan, "Rum. Jam."...) nao
existem no Speech.xwb e ficam sem voz; o script lista quantas sao.

USO (precisa de numpy e soundfile: pip install numpy soundfile)
  python gerar_vozes_monkey1.py MONKEY.SOG SPEECH.XWB

  MONKEY.SOG  o monkey.sog do pacote da traducao (so para gerar a tabela)
  SPEECH.XWB  o audio\\Speech.xwb da Special Edition (GOG)

So precisa rodar de novo se o monkey.sog ou a traducao mudarem.
"""

import collections
import io
import os
import struct
import sys

import numpy as np
import soundfile as sf

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SAIDA = os.path.join(RAIZ, "engines", "scumm", "monkey1_vozes.inc")

# Correlacao minima para aceitar uma fala (as falas curtas, como os grunhidos,
# ficam por volta de 0,83)
CORRELACAO_MINIMA = 0.8


def ler_sog(caminho):
    """[(offset, ogg bytes)] do monkey.sog. O indice tem, por fala: offset no
    arquivo original, posicao do dado, tamanho dos tags (sincronia da boca) e
    tamanho do audio."""
    f = open(caminho, "rb")
    cabecalho, = struct.unpack(">I", f.read(4))
    indice = [struct.unpack(">4I", f.read(16)) for _ in range(cabecalho // 16)]
    falas = []
    for original, novo, tags, tamanho in indice:
        f.seek(novo + cabecalho + 4)
        dados = f.read(tags * 2 + tamanho)
        falas.append((original, dados[dados.find(b"OggS"):]))
    return falas


def ler_xwb(caminho):
    """Lista de (nome, canais, amostras, offset dos dados), do Speech.xwb."""
    f = open(caminho, "rb")
    magico, _versao, _cab = struct.unpack("<4sII", f.read(12))
    if magico != b"WBND":
        raise SystemExit("%s nao e um XWB" % caminho)
    seg = [struct.unpack("<II", f.read(8)) for _ in range(5)]
    f.seek(seg[0][0])
    _flags, total = struct.unpack("<II", f.read(8))
    f.read(64)
    tam_entrada, = struct.unpack("<I", f.read(4))
    f.seek(seg[1][0])
    entradas = []
    for _ in range(total):
        d = f.read(tam_entrada)
        _, formato, off, tam = struct.unpack("<4I", d[:16])
        codec, canais = formato & 3, (formato >> 2) & 7
        if codec != 0:
            raise SystemExit("O Speech.xwb tem audio que nao e PCM; esta tabela so entende o da GOG")
        entradas.append((canais, tam, off + seg[4][0]))
    if not seg[3][0]:
        raise SystemExit("O Speech.xwb nao tem os nomes das falas")
    f.seek(seg[3][0])
    nomes = [f.read(64).split(b"\0")[0].decode("latin1").lower() for _ in range(total)]
    return f, [(nomes[i], c, t // (2 * c), off) for i, (c, t, off) in enumerate(entradas)]


def pcm(f, entrada):
    _nome, canais, amostras, off = entrada
    f.seek(off)
    a = np.frombuffer(f.read(amostras * 2 * canais), dtype="<i2").astype(np.float32) / 32768.0
    return a.reshape(-1, canais).mean(axis=1) if canais > 1 else a


def correlacao(a, b):
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    den = np.sqrt((a * a).sum() * (b * b).sum())
    return float((a * b).sum() / den) if den > 0 else 0.0


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1

    falas = ler_sog(sys.argv[1])
    xwb, entradas = ler_xwb(sys.argv[2])
    por_duracao = collections.defaultdict(list)
    for i, e in enumerate(entradas):
        por_duracao[e[2]].append(i)

    tabela = {}
    sem_candidata = baixa = 0
    for original, ogg in falas:
        audio, taxa = sf.read(io.BytesIO(ogg), dtype="float32")
        if audio.ndim > 1:
            audio = audio.mean(axis=1)
        candidatas = por_duracao.get(len(audio), []) if taxa == 44100 else []
        if not candidatas:
            sem_candidata += 1
            continue
        # Falas com audio igual (a mesma fala em cenas diferentes) empatam:
        # fica a primeira, que soa igual
        pontos = sorted(((-correlacao(audio, pcm(xwb, entradas[i])), i) for i in candidatas))
        melhor, indice = -pontos[0][0], pontos[0][1]
        if melhor < CORRELACAO_MINIMA:
            baixa += 1
            continue
        tabela[original] = entradas[indice][0]

    print("%d falas no monkey.sog: %d ligadas, %d sem fala igual no Speech.xwb, %d com correlacao baixa"
          % (len(falas), len(tabela), sem_candidata, baixa))

    with open(SAIDA, "w", newline="\n") as f:
        f.write("// Generated by devtools/scummbr/gerar_vozes_monkey1.py. Do not edit.\n")
        f.write("// For each speech offset of the Monkey Island 1 translation (the Ultimate Talkie\n")
        f.write("// Edition scripts), the name of the line in the Special Edition's Speech.xwb.\n")
        f.write("// Sorted by offset.\n\n")
        f.write("struct MonkeyTraducaoVoice {\n\tuint32 offset;\n\tconst char *name;\n};\n\n")
        f.write("static const MonkeyTraducaoVoice kMonkeyTraducaoVoices[] = {\n")
        for offset in sorted(tabela):
            f.write('\t{ %d, "%s" },\n' % (offset, tabela[offset]))
        f.write("};\n")
    print("Gravado em", os.path.relpath(SAIDA, RAIZ))
    return 0


if __name__ == "__main__":
    sys.exit(main())
