#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Gera a tabela de vozes do Monkey Island 2 traduzido (engines/scumm/monkey2_vozes.inc).

A traducao do Monkey Island 2 foi feita sobre a Ultimate Talkie Edition (UTE).
No texto da UTE, cada fala tem um "endereco" (offset) que o jogo usa para achar
o audio no monster.sou. O ScummVMBR nao leva o monster.sou: ele toca a fala do
Speech.xwb da Special Edition do proprio jogador (a UTE foi feita a partir
desse mesmo audio). Esta tabela diz, para cada endereco, qual fala do
Speech.xwb tocar. Ela guarda so numeros e nomes, nenhum audio.

Como a tabela e feita: cada fala do monster.sou (VOC, 8 bits) e comparada com
as falas do Speech.xwb (ADPCM) pelo contorno de volume ao longo do tempo, e
fica a de maior correlacao entre as de duracao parecida. O nome da fala vem do
SpeechCues.xsb, como na engine.

Falas que a UTE montou emendando outras nao existem no Speech.xwb e ficam sem
voz; o script lista quantas sao.

USO (precisa de numpy: pip install numpy)
  python gerar_vozes_monkey2.py MONSTER.SOU PASTA_AUDIO

  MONSTER.SOU  o monster.sou do pacote da traducao (so para gerar a tabela)
  PASTA_AUDIO  a pasta audio da Special Edition (GOG), com Speech.xwb e
               SpeechCues.xsb

So precisa rodar de novo se o monster.sou ou a traducao mudarem.
"""

import bisect
import os
import struct
import sys

import numpy as np

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SAIDA = os.path.join(RAIZ, "engines", "scumm", "monkey2_vozes.inc")

# Quadros de contorno de volume por segundo
QUADROS = 200

# A fala do Speech.xwb e um pouco mais longa que a do monster.sou (uns 20 ms a
# 50 ms); so entram na comparacao as de duracao parecida, em segundos
JANELA_MENOS = 0.10
JANELA_MAIS = 0.25

# O audio de 8 bits do monster.sou tem um chiado de fundo que o do Speech.xwb
# nao tem; volumes abaixo disto contam como silencio (em escala de 0 a 1)
PISO = 0.06

# Correlacao minima para aceitar uma fala. Entre 0,80 e 0,90 a melhor tem que
# ganhar da segunda por esta margem, senao a fala fica sem voz
CORRELACAO_MINIMA = 0.80
CORRELACAO_SEGURA = 0.90
MARGEM_MINIMA = 0.03

# Coeficientes do ADPCM da Microsoft
COEF1 = np.array([256, 512, 0, 192, 240, 460, 392], dtype=np.int32)
COEF2 = np.array([0, -256, 0, 64, 0, -208, -232], dtype=np.int32)
ADAPT = np.array([230, 230, 230, 230, 307, 409, 512, 614,
                  768, 614, 512, 409, 307, 230, 230, 230], dtype=np.int32)


def ler_xwb(caminho):
    """Lista de (canais, amostras, taxa, bytes por bloco, offset, tamanho)."""
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
        _, formato, off, tam, _inicio, amostras = struct.unpack("<6I", d[:24])
        codec, canais = formato & 3, (formato >> 2) & 7
        if codec != 2:
            raise SystemExit("O Speech.xwb tem audio que nao e ADPCM; esta tabela so entende o da GOG")
        entradas.append((canais, amostras, (formato >> 5) & 0x3FFFF, ((formato >> 23) & 0xFF) + 22, off + seg[4][0], tam))
    return f, entradas


def nomes_xsb(caminho, total):
    """Nome de cada fala do Speech.xwb, do SpeechCues.xsb (como na engine)."""
    d = open(caminho, "rb").read()
    if d[:4] != b"SDBK":
        raise SystemExit("%s nao e um XSB" % caminho)
    qtd, = struct.unpack_from("<I", d, 19)
    nomes_off, = struct.unpack_from("<I", d, 42)
    cues_off, = struct.unpack_from("<I", d, 70)
    indices = []
    p = cues_off
    for _ in range(qtd):
        tag, = struct.unpack_from("<H", d, p)
        indice, = struct.unpack_from("<H", d, p + 2 + 7)
        indices.append(indice)
        p += 2 + 7 + 2 + (8 if tag == 0x0410 else 1)
    nomes = {}
    p = nomes_off
    for indice in indices:
        fim = d.index(b"\0", p)
        if indice < total and indice not in nomes:
            nomes[indice] = d[p:fim].decode("latin1").lower()
        p = fim + 1
    return nomes


def decodifica_adpcm(dados, bloco):
    """ADPCM da Microsoft, mono, 4 bits. Os blocos sao independentes, entao vai
    um laco por amostra, para todos os blocos de uma vez."""
    n = (len(dados) + bloco - 1) // bloco
    b = np.frombuffer(dados + bytes(n * bloco - len(dados)), dtype=np.uint8).reshape(n, bloco)
    pred = np.minimum(b[:, 0], 6).astype(np.int32)
    delta = b[:, 1].astype(np.int32) | (b[:, 2].astype(np.int32) << 8)
    s1 = (b[:, 3].astype(np.int32) | (b[:, 4].astype(np.int32) << 8)).astype(np.int16).astype(np.int32)
    s2 = (b[:, 5].astype(np.int32) | (b[:, 6].astype(np.int32) << 8)).astype(np.int16).astype(np.int32)
    c1, c2 = COEF1[pred], COEF2[pred]
    nib = np.empty((n, (bloco - 7) * 2), dtype=np.int32)
    nib[:, 0::2] = b[:, 7:] >> 4
    nib[:, 1::2] = b[:, 7:] & 15
    saida = np.empty((n, nib.shape[1] + 2), dtype=np.int16)
    saida[:, 0], saida[:, 1] = s2, s1
    for i in range(nib.shape[1]):
        v = nib[:, i]
        sinal = np.where(v >= 8, v - 16, v)
        amostra = np.clip((s1 * c1 + s2 * c2) // 256 + sinal * delta, -32768, 32767)
        saida[:, i + 2] = amostra
        s2, s1 = s1, amostra
        delta = np.maximum((ADAPT[v] * delta) // 256, 16)
    return saida.reshape(-1).astype(np.float32) / 32768.0


def contorno(x, taxa):
    """Volume (log) a QUADROS por segundo."""
    n = max(int(taxa / QUADROS), 1)
    k = len(x) // n
    if k < 2:
        return None
    e = np.sqrt((x[:k * n].reshape(k, n) ** 2).mean(axis=1))
    c = np.log(np.maximum(e, PISO)).astype(np.float32)
    return c - c.mean()


def ler_sou(caminho):
    """[(offset, contorno, duracao)] do monster.sou: por fala, um bloco VCTL
    (tags da boca) e um VOC com um bloco de dados de 8 bits."""
    d = open(caminho, "rb").read()
    pos = 8
    falas = []
    while pos < len(d):
        if d[pos:pos + 4] != b"VCTL":
            raise SystemExit("monster.sou: VCTL esperado no offset %d" % pos)
        tam, = struct.unpack(">I", d[pos + 4:pos + 8])
        voc = pos + tam
        p = voc + struct.unpack("<H", d[voc + 20:voc + 22])[0]
        taxa = comprimento = 0
        dados = b""
        while d[p] != 0:
            comprimento = d[p + 1] | d[p + 2] << 8 | d[p + 3] << 16
            if d[p] == 1:
                taxa = 1000000.0 / (256 - d[p + 4])
                dados = d[p + 6:p + 4 + comprimento]
            p += 4 + comprimento
        x = (np.frombuffer(dados, dtype=np.uint8).astype(np.float32) - 128) / 128.0
        falas.append((pos, contorno(x, taxa), len(x) / taxa if taxa else 0))
        pos = p + 1
    return falas


def correlacao(a, b):
    """Maior correlacao normalizada entre dois contornos, em qualquer atraso."""
    n = 1 << (len(a) + len(b)).bit_length()
    c = np.fft.irfft(np.fft.rfft(b, n) * np.conj(np.fft.rfft(a, n)), n)
    return float(c.max() / (np.sqrt((a * a).sum() * (b * b).sum()) + 1e-9))


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1

    xwb, entradas = ler_xwb(os.path.join(sys.argv[2], "Speech.xwb"))
    nomes = nomes_xsb(os.path.join(sys.argv[2], "SpeechCues.xsb"), len(entradas))
    print("Lendo o Speech.xwb (%d falas)..." % len(entradas))
    contornos = []
    for canais, amostras, taxa, bloco, off, tam in entradas:
        if canais != 1:
            # Umas poucas falas sao estereo (musica cantada); nao sao falas dos scripts
            contornos.append((None, amostras / float(taxa)))
            continue
        xwb.seek(off)
        x = decodifica_adpcm(xwb.read(tam), bloco)[:amostras]
        contornos.append((contorno(x, taxa), amostras / float(taxa)))
    ordem = sorted(range(len(contornos)), key=lambda i: contornos[i][1])
    duracoes = [contornos[i][1] for i in ordem]

    print("Lendo o monster.sou...")
    falas = ler_sou(sys.argv[1])

    tabela = {}
    sem_candidata = baixa = sem_nome = 0
    for n, (offset, c, dur) in enumerate(falas):
        if n % 500 == 0:
            print("  %d/%d" % (n, len(falas)))
        if c is None:
            sem_candidata += 1
            continue
        ini = bisect.bisect_left(duracoes, dur - JANELA_MENOS)
        fim = bisect.bisect_right(duracoes, dur + JANELA_MAIS)
        melhor, segunda, indice = -1.0, -1.0, -1
        for i in ordem[ini:fim]:
            if contornos[i][0] is None:
                continue
            r = correlacao(c, contornos[i][0])
            if r > melhor:
                melhor, segunda, indice = r, melhor, i
            elif r > segunda:
                segunda = r
        if indice < 0:
            sem_candidata += 1
        elif melhor < CORRELACAO_MINIMA or (melhor < CORRELACAO_SEGURA and melhor - segunda < MARGEM_MINIMA):
            baixa += 1
        elif indice not in nomes:
            sem_nome += 1
        else:
            tabela[offset] = nomes[indice]

    print("%d falas no monster.sou: %d ligadas, %d sem fala parecida no Speech.xwb, %d sem certeza, %d sem nome"
          % (len(falas), len(tabela), sem_candidata, baixa, sem_nome))

    with open(SAIDA, "w", newline="\n") as f:
        f.write("// Generated by devtools/scummbr/gerar_vozes_monkey2.py. Do not edit.\n")
        f.write("// For each speech offset of the Monkey Island 2 translation (the Ultimate Talkie\n")
        f.write("// Edition scripts), the name of the line in the Special Edition's Speech.xwb.\n")
        f.write("// Sorted by offset.\n\n")
        f.write("static const MonkeyTraducaoVoice kMonkey2TraducaoVoices[] = {\n")
        for offset in sorted(tabela):
            f.write('\t{ %d, "%s" },\n' % (offset, tabela[offset]))
        f.write("};\n")
    print("Gravado em", os.path.relpath(SAIDA, RAIZ))
    return 0


if __name__ == "__main__":
    sys.exit(main())
