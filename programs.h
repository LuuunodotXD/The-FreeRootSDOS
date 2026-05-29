#ifndef PROGRAMS_H
#define PROGRAMS_H

// Tenta executar um programa. Retorna 1 se reconhecido, 0 se nao.
// cmd    = linha completa digitada ("edit test.txt")
// drive  = drive ativo ('A' ou 'H')
int prog_run(const char *cmd, char drive);

#endif
