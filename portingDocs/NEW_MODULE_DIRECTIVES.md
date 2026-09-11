# Diretivas para criacao de novos modulos

Este documento define o procedimento oficial para criar, registrar, testar e
documentar um novo modulo do AthenaEnv. O objetivo e manter a estrutura atual
consistente com a automacao existente e evitar falhas durante o bootstrap do
QuickJS ou a inicializacao do PS2.

## 1. Classifique o modulo antes de implementa-lo

Antes de criar arquivos, determine se o componente e:

- **Modulo JavaScript nativo**: exporta funcoes ou propriedades para scripts
  QuickJS e fica em `src/modules/<nome>/`.
- **Modulo IOP**: carrega um driver ou servico no IOP por meio do gerenciador
  de modulos, normalmente em `src/module_system.c`.
- **Camada combinada**: possui uma API JavaScript e tambem depende de um modulo
  IOP. Nesse caso, a inicializacao do IOP deve continuar separada do registro
  dos exports QuickJS.

Nao misture responsabilidades sem necessidade. O registro QuickJS deve ser
previsivel e nao deve executar reset de IOP, iniciar loops ou depender de
hardware que ainda nao foi inicializado.

## 2. Crie a estrutura do modulo

Para um modulo JavaScript nativo, crie pelo menos:

```text
src/modules/<nome>/
  module.json
  native/
    <nome>.c
    <nome>.h
  quickjs/
    ath_<nome>.c
    ath_<nome>.h
  <nome>.d.ts
```

Use nomes em minusculas para a pasta e o identificador do modulo. A separacao
entre a implementacao nativa e a interface QuickJS e obrigatoria para modulos
com regra de negocio propria:

- `native/<nome>.c/.h`: estado, algoritmos, ciclo de vida e contrato nativo;
- `quickjs/ath_<nome>.c/.h`: adaptacao de argumentos, valores e erros QuickJS;
- `<nome>.d.ts`: contrato publico consumido pelos scripts.

O arquivo `ath_<nome>.c` nao deve duplicar a regra de negocio. Ele deve chamar
as funcoes do componente nativo e converter os resultados para `JSValue`.
Modulos triviais, sem estado ou regra reutilizavel, podem justificar uma
implementacao em um unico arquivo, mas essa excecao deve ser consciente.

O `System` segue o mesmo padrao: `native/system.c/.h` contem operacoes nativas
de arquivos, tempo, memoria e servicos do console; `quickjs/ath_system.c/.h`
contem a validacao de argumentos, conversao de valores e exports QuickJS.

Para objetos nativos com estado, nao exponha ponteiros como numeros JavaScript.
Use uma classe QuickJS com `JS_SetOpaque`, `JS_GetOpaque2` e um finalizer. O
metodo de destruicao explicita deve limpar o opaque antes de liberar o estado,
para que o finalizer nao faca double-free:

```c
static void object_finalizer(JSRuntime *rt, JSValue value) {
    NativeObject *object = JS_GetOpaque(value, object_class_id);
    if (object) native_object_destroy(object);
}

NativeObject *object = native_object_create();
JSValue value = JS_NewObjectClass(ctx, object_class_id);
JS_SetOpaque(value, object);
```

O adaptador deve rejeitar objetos de outra classe com `JS_GetOpaque2`. Isso
evita ponteiros arbitrarios, use-after-free, double-free e confusao entre
handles de modulos diferentes. A API TypeScript deve documentar esses valores
como objetos opacos, nunca como `number`.

O `module.json` deve descrever o modulo para a automacao. Consulte os
manifestos existentes antes de adicionar campos novos e mantenha o nome usado
no manifesto igual ao nome importado pelos scripts:

```json
{
  "name": "Timer",
  "sources": ["timer.c", "ath_timer.c"],
  "header": "ath_timer.h",
  "types": "timer.d.ts"
}
```

Os campos exatos devem seguir o formato aceito por `tools/modules.js`. Nao
edite os arquivos gerados para compensar um manifesto incorreto.

## 3. Implemente o contrato nativo e a interface QuickJS

O header da implementacao nativa deve expor somente o contrato de negocio:

```c
#ifndef ATH_NATIVE_TIMER_H
#define ATH_NATIVE_TIMER_H

typedef struct AthenaTimer AthenaTimer;

AthenaTimer *timer_create(void);
void timer_pause(AthenaTimer *timer);
void timer_resume(AthenaTimer *timer);
void timer_destroy(AthenaTimer *timer);

#endif
```

O adaptador QuickJS deve expor somente a interface necessaria ao registro:

```c
#ifndef ATH_TIMER_H
#define ATH_TIMER_H

#include <quickjs.h>

JSModuleDef *athena_timer_init(JSContext *ctx);

#endif
```

O arquivo nativo deve conter:

1. o estado interno do componente;
2. as operacoes de negocio;
3. as politicas de ciclo de vida;
4. nenhuma dependencia de `JSContext`, `JSValue` ou QuickJS.

O adaptador `ath_<nome>.c` deve conter:

1. as funcoes JS;
2. a tabela de exports;
3. o inicializador usado pelo registro automatico;
4. validacao de argumentos;
5. conversao entre tipos JS e tipos nativos;
6. tratamento explicito de erros.

Mantenha as funcoes internas como `static` sempre que nao precisarem ser
usadas fora do arquivo. O `module.json` deve listar todos os fontes em
`sources`; o gerador nao deve depender de descoberta implicita de arquivos.

## 4. Registre exports QuickJS de forma compativel

O AthenaEnv declara os exports com `JS_AddModuleExportList` e materializa os
valores com `JS_SetModuleExportList`. Portanto, a mesma tabela precisa ser
valida nos dois caminhos.

Prefira:

```c
static const JSCFunctionListEntry timer_module_funcs[] = {
    JS_CFUNC_DEF("now", 0, athena_timer_now),
    JS_CFUNC_DEF("sleep", 1, athena_timer_sleep),
    JS_PROP_INT32_DEF("version", 1, JS_PROP_ENUMERABLE),
};
```

Tipos normalmente suportados pelo registro atual:

```c
JS_CFUNC_DEF(...)
JS_PROP_STRING_DEF(...)
JS_PROP_INT32_DEF(...)
JS_PROP_INT64_DEF(...)
JS_PROP_FLOAT32_DEF(...)
JS_PROP_FLOAT64_DEF(...)
JS_OBJECT_DEF(...)
```

Nao use diretamente na tabela de exports:

```c
JS_CGETSET_DEF(...)
JS_CGETSET_MAGIC_DEF(...)
```

Antes de usar qualquer outro tipo, confirme o tratamento correspondente em
`src/quickjs/quickjs.c`, especialmente na implementacao de
`JS_SetModuleExportList`. Um tipo nao suportado pode executar `abort()` nativo
durante o bootstrap e aparecer no PCSX2 apenas como reset para o `OSDSYS`.

O inicializador deve seguir o par de registro atual:

```c
static int timer_module_init(JSContext *ctx, JSModuleDef *m) {
    return JS_SetModuleExportList(
        ctx,
        m,
        timer_module_funcs,
        countof(timer_module_funcs)
    );
}

JSModuleDef *athena_timer_init(JSContext *ctx) {
    return athena_push_module(
        ctx,
        timer_module_init,
        timer_module_funcs,
        countof(timer_module_funcs),
        "Timer"
    );
}
```

O nome passado a `athena_push_module`, o nome do manifesto e o nome usado no
bootstrap devem ser consistentes, respeitando maiusculas e minusculas.

## 5. Valide argumentos e erros

Cada funcao deve definir claramente:

- quantidade minima e maxima de argumentos;
- tipos aceitos;
- limites numericos;
- comportamento para strings vazias ou caminhos invalidos;
- codigo de erro retornado por APIs do PS2SDK;
- diferenca entre estado valido e falha real.

Nao trate todo valor negativo como erro sem consultar a documentacao da API.
Algumas APIs usam valores negativos para estados validos, como a primeira
deteccao de um Memory Card por `mcSync`.

Use as convencoes existentes do modulo `System` para:

- `JS_ThrowTypeError` em argumentos invalidos;
- `JS_ThrowRangeError` em valores fora dos limites;
- `JS_ThrowInternalError` em falhas de hardware, RPC ou sistema;
- liberacao de strings com `JS_FreeCString` em todos os caminhos de saida.

Nao adicione `catch` amplo ou retorno silencioso que transforme uma falha em
sucesso aparente.

## 6. Separe inicializacao do registro

O registro do modulo deve somente declarar e criar a API QuickJS. Nao execute
nele:

- `SifIopReset`;
- carregamento de modulos IOP;
- acesso a Memory Card, USB, disco ou GS;
- loops infinitos;
- alocacoes grandes sem validacao;
- chamadas que possam bloquear indefinidamente;
- inicializacao dependente de ordem externa.

Quando a API depender de um modulo IOP, registre o driver no fluxo de
`src/module_system.c` e faca a API JS verificar explicitamente se o servico
esta disponivel antes de usa-lo.

## 7. Atualize a tipagem e os artefatos por automacao

Apos criar ou alterar o modulo:

1. atualize `<nome>.d.ts`;
2. confirme que o manifesto esta correto;
3. execute `tools/modules.js` conforme o comando definido no projeto;
4. regenere o registro de modulos;
5. regenere o catalogo e os typings agregados;
6. confira o diff dos arquivos gerados.

Quando a implementacao for separada, confirme que tanto
`native/<nome>.c` quanto `quickjs/ath_<nome>.c` estao listados em `module.json`.
Adicionar apenas o adaptador
QuickJS gera falha de link ou incentiva a reintroducao da regra de negocio no
arquivo errado.

Arquivos como estes sao derivados e nao devem ser editados manualmente:

```text
src/generated/modules_registry.c
Makefile.modules
catalog.json
public/catalog.json
bin/athena.d.ts
public/athena.d.ts
```

Se um artefato nao for gerado como esperado, corrija o manifesto ou a
automacao em `tools/modules.js`, em vez de aplicar uma alteracao manual no
resultado.

## 8. Escreva testes executaveis no ambiente real

Crie o teste em `bin/tests/<nome>_test.js` seguindo o padrao de
`bin/tests/system_test.js`.

Os testes devem cobrir, no minimo:

- import do modulo;
- formato e tipos dos exports;
- caminho feliz de cada funcao publica;
- argumentos ausentes;
- argumentos extras;
- tipos invalidos;
- limites relevantes;
- recursos dependentes de hardware.

Testes dependentes de hardware devem distinguir indisponibilidade do ambiente
de falha funcional. Use `SKIP` somente quando o recurso realmente nao estiver
disponivel e inclua a causa retornada pela API.

Evite loops infinitos nos scripts de teste. Quando for necessario aguardar um
recurso, use `System.delay()` ou `System.sleep()` com limite definido.

## 9. Ordem obrigatoria de validacao

Valide cada modulo nesta ordem:

1. compile o projeto;
2. confirme que o modulo foi descoberto pela automacao;
3. execute um script minimo que apenas importa o modulo;
4. execute cada export individualmente;
5. valide erros de argumentos;
6. execute a suite do modulo no PCSX2;
7. execute a mesma suite no hardware real, quando aplicavel;
8. confirme que o processo termina sem reset inesperado.

Um script minimo deve ser semelhante a:

```js
import * as Timer from "Timer";
console.log("[test] Timer imported");
```

Durante a investigacao, use o build de debug:

```bash
make clean debug
```

O fluxo QuickJS esperado deve conter:

```text
[AthenaCore] Starting QuickJS runtime...
[AthenaCore] Evaluating base bootstrap
[AthenaCore] Base bootstrap returned 0
[AthenaCore] Evaluating module bootstrap
[AthenaCore] Module bootstrap returned 0
[AthenaCore] Entry script evaluation returned 0
```

Se o ultimo marcador for `Evaluating module bootstrap`, revise primeiro todas
as tabelas de exports e os inicializadores dos modulos registrados.

## 10. Checklist antes de considerar o modulo pronto

- [ ] O modulo possui `module.json`.
- [ ] O nome do modulo e consistente em todos os arquivos.
- [ ] O header possui include guard e declara o inicializador.
- [ ] A tabela de exports usa somente tipos suportados.
- [ ] `JS_AddModuleExportList` e `JS_SetModuleExportList` usam a mesma tabela.
- [ ] As funcoes validam argumentos e limites.
- [ ] Erros do PS2SDK nao sao ignorados.
- [ ] O registro nao inicializa hardware nem reseta o IOP.
- [ ] O arquivo `.d.ts` esta sincronizado com os exports.
- [ ] Os artefatos gerados foram regenerados pela automacao.
- [ ] Existe um teste em `bin/tests`.
- [ ] O import minimo foi executado no PCSX2.
- [ ] Os testes de hardware diferenciam `SKIP` de falha.
- [ ] O comportamento foi validado no hardware alvo, quando disponivel.
- [ ] O modulo foi documentado quando possui limitacoes especificas.

## 11. Regras para a migracao de modulos antigos

Ao portar um modulo de `old/`:

1. use o modulo antigo como referencia de comportamento, nao como codigo
   para copiar integralmente;
2. compare assinaturas, constantes e retornos com a versao atual do PS2SDK;
3. remova dependencias de modulos que nao existem na estrutura atual;
4. preserve compatibilidade da API somente quando ela nao comprometer a
   seguranca ou o contrato atual;
5. adapte o registro ao modelo automatico de `tools/modules.js`;
6. documente qualquer diferenca intencional entre a API antiga e a nova;
7. teste primeiro a fundacao da qual o modulo depende.

A migracao esta concluida somente quando o modulo compila, e descoberto pela
automacao, pode ser importado pelo QuickJS e possui testes reproduziveis.
