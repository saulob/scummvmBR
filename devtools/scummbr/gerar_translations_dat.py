#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Gera o gui/themes/translations.dat (traducao da interface do ScummVM) a
partir dos po/*.po, sem compilar nada.

E uma copia fiel do devtools/create_translations (create_translations.cpp e
po_parser.cpp), que o "make translations-dat" usa. O build do Visual Studio
embute o translations.dat pronto (dists/scummvm.rc), entao depois de mudar
um .po e preciso rodar isto e recompilar.

USO
  python devtools/scummbr/gerar_translations_dat.py
  python devtools/scummbr/gerar_translations_dat.py SAIDA.dat ARQUIVO.po [...]
"""

import glob
import os
import re
import struct
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
VERSAO = 4


def comparar(a, b):
    """strcmp"""
    return (a > b) - (a < b)


def linhas_do_po(caminho):
    """Linhas como o fgets(line, 1024, ...) do po_parser.cpp as le: no maximo
    1023 bytes por vez. CRLF vira LF, como num checkout sem autocrlf."""
    with open(caminho, "rb") as f:
        dados = f.read().replace(b"\r\n", b"\n")
    partes = dados.split(b"\n")
    for i, parte in enumerate(partes):
        linha = parte + b"\n" if i < len(partes) - 1 else parte
        while linha:
            yield linha[:1023]
            linha = linha[1023:]


def tirar_aspas(linha):
    """stripLine: o texto entre as duas primeiras aspas, com os escapes."""
    i = linha.find(b'"')
    if i < 0:
        return b""
    i += 1
    escapes = {ord("n"): 10, ord("t"): 9, ord('"'): ord('"'), ord("'"): ord("'"), ord("\\"): ord("\\")}
    saida = bytearray()
    while i < len(linha) and linha[i] != ord('"'):
        c = linha[i]
        i += 1
        if c == ord("\\"):
            c = linha[i] if i < len(linha) else 0
            i += 1
            if c not in escapes:
                print('Caractere especial "\\%c" nao suportado.' % c, file=sys.stderr)
                continue
            c = escapes[c]
        saida.append(c)
    return bytes(saida)


def campo_do_cabecalho(texto, campo):
    """parseLine"""
    i = texto.find(campo)
    if i < 0:
        return None
    i += len(campo)
    while i < len(texto) and texto[i] in b" \t\n\v\f\r":
        i += 1
    fim = texto.find(b"\n", i)
    valor = texto[i:] if fim < 0 else texto[i:fim]
    return valor or None


class Entrada:
    def __init__(self, msgstr, msgid, msgctxt):
        self.msgstr = msgstr
        self.msgid = msgid
        self.msgctxt = msgctxt or None


class Idioma:
    """PoMessageEntryList"""

    def __init__(self, codigo):
        self.codigo = codigo
        self.nome = None
        self.nome_alt = codigo
        self.utf8 = True
        self.entradas = []

    def nome_exibido(self):
        return self.nome if self.nome is not None else self.nome_alt

    def adicionar(self, traducao, mensagem, contexto):
        """addMessageEntry. contexto nunca e NULL: o parser passa o buffer,
        vazio quando a entrada nao tem msgctxt."""
        if mensagem == b"":
            nome = campo_do_cabecalho(traducao, b"X-Language-name:")
            if nome is not None:
                self.nome = nome
            nome = campo_do_cabecalho(traducao, b"Language:")
            if nome is not None:
                self.nome_alt = nome
            if campo_do_cabecalho(traducao, b"charset=") not in (b"utf-8", b"UTF-8"):
                self.utf8 = False
            return

        lista = self.entradas
        esq, dir_ = 0, len(lista) - 1
        while dir_ >= esq:
            meio = (esq + dir_) // 2
            r = comparar(mensagem, lista[meio].msgid)
            if r == 0:
                if lista[meio].msgctxt is None:
                    r = 1
                else:
                    r = comparar(contexto, lista[meio].msgctxt)
                    if r == 0:
                        return
            if r < 0:
                dir_ = meio - 1
            else:
                esq = meio + 1

        # Traducao igual a da entrada sem contexto: desnecessaria
        if contexto:
            i = esq - 1
            while i >= 0 and lista[i].msgid == mensagem:
                i -= 1
            i += 1
            if i < esq and lista[i].msgctxt is None and traducao == lista[i].msgstr:
                return

        lista.insert(esq, Entrada(traducao, mensagem, contexto))

        if not contexto:
            # Remove as entradas com contexto que repetem esta traducao
            i = esq + 1
            mantidas = lista[:i]
            while i < len(lista) and lista[i].msgid == mensagem:
                if traducao != lista[i].msgstr:
                    mantidas.append(lista[i])
                i += 1
            self.entradas = mantidas + lista[i:]


def ler_po(caminho, mensagens):
    """parsePoFile"""
    idioma = Idioma(os.path.basename(caminho).split(".")[0].encode("ascii"))
    msgid = msgstr = msgctxt = b""
    atual = "msgstr"
    fuzzy = proximo_fuzzy = False

    def guardar():
        if msgstr and not fuzzy:
            if msgid:
                mensagens.add(msgid)
            idioma.adicionar(msgstr, msgid, msgctxt)

    for linha in linhas_do_po(caminho):
        if linha.startswith(b"#,") and b"fuzzy" in linha:
            proximo_fuzzy = True
            continue
        if linha.startswith(b"\n") or linha.startswith(b"#"):
            continue
        if linha.startswith(b"msgid") or linha.startswith(b"msgctxt"):
            if atual == "msgstr":
                guardar()
                msgid = msgstr = msgctxt = b""
                fuzzy, proximo_fuzzy = proximo_fuzzy, False
            if linha.startswith(b"msgid"):
                msgid, atual = tirar_aspas(linha), "msgid"
            else:
                msgctxt, atual = tirar_aspas(linha), "msgctxt"
        elif linha.startswith(b"msgstr"):
            msgstr, atual = tirar_aspas(linha), "msgstr"
        elif atual == "msgid":
            msgid += tirar_aspas(linha)
        elif atual == "msgctxt":
            msgctxt += tirar_aspas(linha)
        else:
            msgstr += tirar_aspas(linha)
    if atual == "msgstr":
        guardar()
    return idioma


def texto(s):
    """writeString: tamanho (uint16 BE, com o \\0) e o texto."""
    if s is None:
        return struct.pack(">H", 0)
    return struct.pack(">H", len(s) + 1) + s + b"\0"


def ordem_do_shell(caminho):
    """Ordem dos idiomas no arquivo oficial: a do glob do shell no Linux, que
    ignora pontuacao e maiusculas ("be_BY" antes de "be-tarask", "zh" depois
    de "zh_Hant")."""
    nome = os.path.basename(caminho)
    return (re.sub(r"[^0-9a-z]", "", nome.lower()), nome)


def gerar(saida, arquivos_po):
    arquivos_po = sorted(arquivos_po, key=ordem_do_shell)
    conjunto = set()
    idiomas = [ler_po(p, conjunto) for p in arquivos_po]
    for idioma in idiomas:
        if not idioma.utf8:
            raise SystemExit("ERRO: %s nao esta em UTF-8" % idioma.codigo.decode())
    mensagens = sorted(conjunto)
    indice = {m: i for i, m in enumerate(mensagens)}

    descricao = b"".join(texto(i.codigo) + texto(i.nome_exibido()) for i in idiomas)
    originais = struct.pack(">H", len(mensagens)) + b"".join(texto(m) for m in mensagens)
    blocos = []
    for idioma in idiomas:
        bloco = struct.pack(">H", len(idioma.entradas))
        for e in idioma.entradas:
            bloco += struct.pack(">H", indice[e.msgid]) + texto(e.msgstr) + texto(e.msgctxt)
        blocos.append(bloco)

    dados = b"TRANSLATIONS" + struct.pack(">BH", VERSAO, len(idiomas))
    dados += struct.pack(">II", len(descricao), len(originais))
    dados += b"".join(struct.pack(">I", len(b)) for b in blocos)
    dados += descricao + originais + b"".join(blocos)

    with open(saida, "wb") as f:
        f.write(dados)
    return len(idiomas), len(mensagens), len(dados)


def main():
    if len(sys.argv) > 2:
        saida, arquivos = sys.argv[1], sys.argv[2:]
    elif len(sys.argv) == 1:
        saida = os.path.join(RAIZ, "gui", "themes", "translations.dat")
        arquivos = sorted(glob.glob(os.path.join(RAIZ, "po", "*.po")))
    else:
        print(__doc__)
        return 1
    idiomas, mensagens, tamanho = gerar(saida, arquivos)
    print("%s: %d idiomas, %d mensagens, %d bytes" % (saida, idiomas, mensagens, tamanho))
    return 0


if __name__ == "__main__":
    sys.exit(main())
