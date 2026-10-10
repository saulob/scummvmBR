#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Gera os .patch das traducoes embutidas do ScummVMBR (dists/traducoes/<jogo>).

Compara cada arquivo traduzido do jogo com o original e grava so a diferenca,
em <ARQUIVO>.patch. Com a opcao "Traduzir o jogo" marcada, o ScummVMBR aplica o
.patch em memoria (engines/traducoes.cpp); os arquivos do jogo nunca mudam.

O original pode vir de dois jeitos:
  - os patchers de traducao (simon1_ptbr, simon2_ptbr...) reescrevem os
    arquivos do jogo e guardam o original ao lado, como <ARQUIVO>.bkp;
  - uma pasta com o jogo original e outra com o jogo ja traduzido, como nos
    pacotes que ja vem com a traducao aplicada.

Os .patch de dists/traducoes vao dentro do executavel: no fim, este script
sempre regrava o engines/traducoes_embutidas.inc com todos eles, comprimidos
com zlib. Depois e so recompilar o ScummVMBR.

USO
  python gerar_traducao.py PASTA_DO_JOGO_TRADUZIDO PASTA_DE_SAIDA
  python gerar_traducao.py PASTA_ORIGINAL PASTA_TRADUZIDA PASTA_DE_SAIDA [ARQUIVO...]
  python gerar_traducao.py --embutir      (so regrava o .inc)

  Com tres pastas, ARQUIVO sao os arquivos do jogo a comparar, e aceitam
  * e ? ("*.lfl"); maiusculas e minusculas tanto faz. Sem ARQUIVO, entram
  todos os arquivos diferentes da pasta (sem subpastas), entao confira a lista:
  arquivos que o jogo nao le (.ini, .lnk, desinstalador...) nao devem entrar.
  O .patch leva o nome do arquivo na PASTA_ORIGINAL.

  Para atualizar a traducao embutida do Simon 1, depois de rodar o
  simon1_ptbr.py na pasta do jogo:
    python devtools/scummbr/gerar_traducao.py D:/jogos/simon1 dists/traducoes/simon1

  Para o Day of the Tentacle, com o jogo original em ING e o traduzido em PTBR:
    python devtools/scummbr/gerar_traducao.py ING PTBR dists/traducoes/tentacle tentacle.000 tentacle.001

  Para testar sem recompilar, grave numa pasta "traducoes" ao lado do
  executavel; ela tem prioridade sobre a traducao embutida:
    python devtools/scummbr/gerar_traducao.py D:/jogos/simon1 S:/scummvmBR/scummvm/traducoes/simon1

FORMATO DO .patch (little endian, lido por engines/traducoes.cpp)
  char[8]   "SBRPTCH1"
  uint32    tamanho do arquivo original
  byte[16]  md5 do arquivo original
  uint32    tamanho do arquivo traduzido
  uint32    numero de operacoes
  operacoes, na ordem em que montam o arquivo traduzido:
    byte 0, uint32 posicao, uint32 tamanho   copia do arquivo original
    byte 1, uint32 tamanho, bytes            dados novos
"""

import fnmatch
import hashlib
import io
import os
import struct
import sys
import zlib

MAGICO = b"SBRPTCH1"
COPIA = 0
DADOS = 1

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TRADUCOES = os.path.join(RAIZ, "dists", "traducoes")
INC = os.path.join(RAIZ, "engines", "traducoes_embutidas.inc")

# Tamanho dos blocos do original usados para achar trechos iguais. Trecho
# igual menor que isso vira dado novo, o que so aumenta um pouco o .patch.
BLOCO = 32


def iguais(orig, novo, o, n):
    """Quantos bytes seguidos sao iguais a partir de orig[o] e novo[n]."""
    limite = min(len(orig) - o, len(novo) - n)
    total = 0
    passo = 1 << 16
    while passo:
        while total + passo <= limite and orig[o + total:o + total + passo] == novo[n + total:n + total + passo]:
            total += passo
        passo >>= 1
    return total


def diferenca(orig, novo):
    """Lista de operacoes (COPIA, posicao, tamanho) / (DADOS, bytes) que monta
    o novo a partir do original."""
    orig = memoryview(orig)
    novo = memoryview(novo)

    indice = {}
    for p in range(0, len(orig) - BLOCO + 1, BLOCO):
        indice.setdefault(orig[p:p + BLOCO].tobytes(), p)

    ops = []
    pendente = 0        # inicio dos dados novos ainda nao gravados
    pos = 0
    seguinte = -1       # no original, logo depois da ultima copia
    while pos + BLOCO <= len(novo):
        bloco = novo[pos:pos + BLOCO]
        if seguinte >= 0 and orig[seguinte:seguinte + BLOCO] == bloco:
            achado = seguinte
        else:
            achado = indice.get(bloco.tobytes(), -1)
        if achado < 0:
            pos += 1
            continue

        # Estende o trecho igual para tras (sobre os dados novos pendentes)
        # e para frente.
        tras = 0
        while pos - tras > pendente and achado - tras > 0 and novo[pos - tras - 1] == orig[achado - tras - 1]:
            tras += 1
        inicio = pos - tras
        origem = achado - tras
        tamanho = tras + BLOCO + iguais(orig, novo, achado + BLOCO, pos + BLOCO)

        if inicio > pendente:
            ops.append((DADOS, novo[pendente:inicio].tobytes()))
        ops.append((COPIA, origem, tamanho))
        pos = pendente = inicio + tamanho
        seguinte = origem + tamanho

    if pendente < len(novo):
        ops.append((DADOS, novo[pendente:].tobytes()))
    return ops


def montar_patch(orig, novo, ops):
    saida = bytearray(MAGICO)
    saida += struct.pack("<I", len(orig)) + hashlib.md5(orig).digest()
    saida += struct.pack("<II", len(novo), len(ops))
    for op in ops:
        if op[0] == COPIA:
            saida += struct.pack("<BII", COPIA, op[1], op[2])
        else:
            saida += struct.pack("<BI", DADOS, len(op[1])) + op[1]
    return bytes(saida)


def aplicar(patch, orig):
    """Le o .patch como o engines/traducoes.cpp le, para conferir o resultado."""
    if patch[:8] != MAGICO:
        raise ValueError("assinatura errada")
    tam_orig, = struct.unpack_from("<I", patch, 8)
    if tam_orig != len(orig) or patch[12:28] != hashlib.md5(orig).digest():
        raise ValueError("o .patch nao e deste original")
    tam_novo, quantas = struct.unpack_from("<II", patch, 28)
    pos = 36
    saida = bytearray()
    for _ in range(quantas):
        tipo = patch[pos]
        if tipo == COPIA:
            origem, tamanho = struct.unpack_from("<II", patch, pos + 1)
            saida += orig[origem:origem + tamanho]
            pos += 9
        else:
            tamanho, = struct.unpack_from("<I", patch, pos + 1)
            saida += patch[pos + 5:pos + 5 + tamanho]
            pos += 5 + tamanho
    if pos != len(patch) or len(saida) != tam_novo:
        raise ValueError("tamanho errado")
    return bytes(saida)


def ler(caminho):
    with io.open(caminho, "rb") as f:
        return f.read()


def embutir():
    """Grava no .inc todos os dists/traducoes/<jogo>/*.patch, para irem dentro
    do executavel."""
    patches = []
    for jogo in sorted(os.listdir(TRADUCOES)):
        pasta = os.path.join(TRADUCOES, jogo)
        if not os.path.isdir(pasta):
            continue
        for nome in sorted(os.listdir(pasta)):
            if nome.lower().endswith(".patch"):
                patches.append((jogo, nome[:-len(".patch")], ler(os.path.join(pasta, nome))))

    # Comprimidos, o .inc e o executavel ficam bem menores. Em decimal e sem
    # espacos, o .inc tambem fica menor do que em hexadecimal.
    comprimidos = [zlib.compress(dados, 9) for _j, _a, dados in patches]

    with io.open(INC, "w", newline="\n") as f:
        f.write("// Generated by devtools/scummbr/gerar_traducao.py from dists/traducoes. Do not edit.\n")
        f.write("// Each array is one <FILE>.patch compressed with zlib; gerar_traducao.py documents the format.\n")
        for (jogo, arquivo, _d), dados in zip(patches, comprimidos):
            f.write("\nstatic const byte %s[%d] = {\n" % (nome_c(jogo, arquivo), len(dados)))
            for i in range(0, len(dados), 32):
                f.write("%s,\n" % ",".join("%d" % b for b in dados[i:i + 32]))
            f.write("};\n")
        f.write("\nstatic const EmbeddedPatch kEmbeddedPatches[] = {\n")
        for (jogo, arquivo, dados), comprimido in zip(patches, comprimidos):
            f.write('\t{ "%s", "%s", %s, %d, %d },\n' % (jogo, arquivo, nome_c(jogo, arquivo), len(comprimido), len(dados)))
        f.write("\t{ nullptr, nullptr, nullptr, 0, 0 }\n};\n")

    print("%s: %d patch(es), %d bytes (%d comprimidos)" % (os.path.relpath(INC, RAIZ), len(patches),
                                                          sum(len(d) for _j, _a, d in patches),
                                                          sum(len(c) for c in comprimidos)))


def nome_c(jogo, arquivo):
    return "kTraducao_" + "".join(c if c.isalnum() else "_" for c in jogo + "_" + arquivo)


def pares_bkp(jogo):
    """(original, traduzido, nome) de cada <ARQUIVO>.bkp com o <ARQUIVO> ao lado."""
    pares = []
    for nome in sorted(os.listdir(jogo)):
        if nome.lower().endswith(".bkp"):
            traduzido = os.path.join(jogo, nome[:-4])
            if os.path.isfile(traduzido):
                pares.append((os.path.join(jogo, nome), traduzido, nome[:-4]))
    if not pares:
        print("Nenhum <ARQUIVO>.bkp com o <ARQUIVO> traduzido ao lado em: %s" % jogo)
        print("Rode o patcher de traducao nessa pasta antes.")
    return pares


def pares_pastas(original, traduzida, mascaras):
    """(original, traduzido, nome) de cada arquivo das duas pastas que entra no
    patch. Sem mascaras, entram os arquivos diferentes."""
    def arquivos(pasta):
        return dict((nome.lower(), os.path.join(pasta, nome)) for nome in os.listdir(pasta)
                    if os.path.isfile(os.path.join(pasta, nome)))

    orig, trad = arquivos(original), arquivos(traduzida)
    for m in mascaras:
        if not any(fnmatch.fnmatchcase(chave, m.lower()) for chave in orig):
            print("Nenhum arquivo %s em %s" % (m, original))
            return []

    pares = []
    for chave in sorted(orig):
        nome = os.path.basename(orig[chave])
        if mascaras:
            if not any(fnmatch.fnmatchcase(chave, m.lower()) for m in mascaras):
                continue
            if chave not in trad:
                print("%-12s nao existe em %s" % (nome, traduzida))
                return []
        elif chave not in trad or ler(orig[chave]) == ler(trad[chave]):
            continue
        pares.append((orig[chave], trad[chave], nome))

    if not pares:
        print("Nenhum arquivo diferente entre %s e %s" % (original, traduzida))
    return pares


def main():
    args = sys.argv[1:]
    if args == ["--embutir"]:
        embutir()
        return 0
    if len(args) == 2:
        pares = pares_bkp(args[0])
        saida = args[1]
    elif len(args) >= 3 and os.path.isdir(args[0]) and os.path.isdir(args[1]):
        pares = pares_pastas(args[0], args[1], args[3:])
        saida = args[2]
    else:
        print(__doc__)
        return 1
    if not pares:
        return 1

    os.makedirs(saida, exist_ok=True)
    gerados = set()
    for original, traduzido, nome in pares:
        orig = ler(original)
        novo = ler(traduzido)
        if orig == novo:
            print("%-12s igual ao original (jogo nao traduzido?), pulado" % nome)
            continue
        ops = diferenca(orig, novo)
        patch = montar_patch(orig, novo, ops)
        if aplicar(patch, orig) != novo:
            print("%-12s ERRO: o .patch nao reproduz o arquivo traduzido" % nome)
            return 1
        with io.open(os.path.join(saida, nome + ".patch"), "wb") as f:
            f.write(patch)
        gerados.add((nome + ".patch").lower())
        novos = sum(len(op[1]) for op in ops if op[0] == DADOS)
        print("%-12s %9d bytes de patch (%d bytes novos, %d operacoes)" % (nome, len(patch), novos, len(ops)))

    sobras = [n for n in os.listdir(saida) if n.lower().endswith(".patch") and n.lower() not in gerados]
    for nome in sobras:
        print("ATENCAO: %s nao foi gerado agora; apague-o se nao vale mais." % nome)

    embutir()
    return 0


if __name__ == "__main__":
    sys.exit(main())
