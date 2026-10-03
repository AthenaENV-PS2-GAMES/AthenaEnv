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
  de modulos. Drivers de boot ficam em `src/core/iop_registry.c`; drivers
  usados por um unico modulo sao embutidos pelo proprio modulo (`embed_irx`).
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
  include/athena/
    <nome>.h          API C publica: #include <athena/<nome>.h>
  native/
    <nome>.c
    <interno>.h       headers privados, incluidos com aspas relativas
  quickjs/
    ath_<nome>.c
    ath_<nome>.h
  <nome>.d.ts
```

So o `include/` dos modulos selecionados entra no include path. Um modulo que
usa outro precisa declara-lo em `dependencies.modules`; caso contrario, o
`#include <athena/<outro>.h>` falha ao compilar o modulo sozinho. Headers de
binding QuickJS usados por outros modulos ficam em `include/athena/js/`.

Use nomes em minusculas para a pasta e o identificador do modulo. A separacao
entre a implementacao nativa e a interface QuickJS e obrigatoria para modulos
com regra de negocio propria:

- `include/athena/<nome>.h` + `native/<nome>.c`: estado, algoritmos, ciclo de
  vida e contrato nativo;
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

Registre a classe no inicializador do modulo com `athena_register_class()`
(`ath_env.h`), nunca protegida por uma flag `static bool`. O id da classe e
global ao processo, mas o registro pertence ao runtime: com uma flag, um
runtime recriado ficaria sem a classe e `JS_NewObjectClass` falharia. O
prototipo pertence ao contexto e deve ser definido em toda inicializacao:

```c
static int module_init(JSContext *ctx, JSModuleDef *m) {
    if (athena_register_class(ctx, &object_class_id, &object_class) < 0)
        return -1;
    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, object_proto, countof(object_proto));
    JS_SetClassProto(ctx, object_class_id, proto);
    ...
}
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
  "id": "timer",
  "name": "Timer",
  "sources": ["native/timer.c"],
  "dependencies": { "modules": [], "iop": [], "ee_libs": [] },
  "native": {
    "init": "athena_timer_module_init",
    "shutdown": "athena_timer_module_shutdown"
  },
  "quickjs": {
    "sources": ["quickjs/ath_timer.c"],
    "module_name": "Timer",
    "global_alias": "Timer",
    "init_func": "athena_timer_init"
  },
  "types": "timer.d.ts"
}
```

- `sources`: somente a implementacao nativa. E compilada nos dois runtimes
  (`RUNTIME=quickjs` e `RUNTIME=native`) e nao pode incluir QuickJS.
  Aceita C (`.c`), assembly do EE (`.s`, montado com `$(EE_AS)`) e
  microprogramas VU pre-compilados (`.vsm`). Bibliotecas de terceiros podem
  ser vendorizadas dentro do modulo e listadas aqui (ex.: o `libmpeg` do
  `video` em `native/libmpeg/`), com um README registrando as mudancas em
  relacao ao original.
- `quickjs.sources`: adaptadores QuickJS. So entram com `RUNTIME=quickjs`.
- `includes` (opcional): somente caminhos externos, como
  `$(PS2DEV)/gsKit/include`. Headers do proprio modulo ficam em `include/`.
- `native` (opcional): hooks de ciclo de vida chamados pelo core em ambos os
  runtimes, na ordem de dependencia. Todos sao `(void)`:
  - `register_iop` registra os drivers IRX do modulo antes do reset do IOP
    no boot (ex.: `memcard`, `usbmass`, `cdrom`);
  - `init` retorna `int` (< 0 aborta o boot);
  - `shutdown` roda em ordem inversa ao encerrar;
  - `quiesce` aguarda trabalho em segundo plano antes de o runtime ser
    destruido (ex.: `thread`);
  - `stop_requested` retorna != 0 para pedir que a aplicacao pare.
  O core nunca chama funcoes de um modulo diretamente: qualquer interacao
  passa por esses hooks, para que o modulo possa ser removido do build.
- `embed` (opcional): arquivos embutidos com bin2c como `<name>[]` e
  `size_<name>`, apenas quando o modulo esta no build (ex.: a fonte padrao do
  `font`, o `loader_elf` do `system`). `build` indica um diretorio que gera o
  arquivo.
- `build.export_symbols`: somente o modulo `erl` usa. Gera a tabela de
  simbolos do binario e desliga o `--gc-sections`.

Os campos exatos devem seguir o formato aceito por `tools/modules.js`. Nao
edite os arquivos gerados para compensar um manifesto incorreto.

Modulos que dependem de drivers IOP fora do nucleo os declaram em
`embed_irx`. Cada entrada gera os simbolos `<name>_irx` e `size_<name>_irx`,
e so entra no ELF quando o modulo esta ativo. `build` indica um diretorio de
`iop_modules/` que e recompilado quando seus fontes mudam e limpo por
`make clean`:

```json
"embed_irx": [
  { "name": "mtapman", "irx": "$(PS2SDK)/iop/irx/mtapman.irx" },
  { "name": "ds34usb", "irx": "iop_modules/ds34usb/iop/ds34usb.irx",
    "build": "iop_modules/ds34usb/iop" }
]
```

O modulo registra esses drivers no IOP manager, nunca durante o registro
QuickJS:

- drivers que precisam existir no boot (dispositivos de armazenamento,
  drivers iniciados por `athena.ini`) sao registrados no hook
  `native.register_iop`;
- drivers usados sob demanda podem ser registrados na primeira vez que forem
  necessarios (veja `src/modules/gamepad/native/gamepad_iop.c`).

Use `iopman_ensure_module_buffer()` quando o driver puder ser compartilhado
por outro modulo (`sio2man` em `memcard` e `gamepad`, `usbd` em `usbmass`
e `gamepad`): ele reaproveita o registro existente. Os dois modulos declaram
o mesmo `embed_irx`; o gerador embute o arquivo uma unica vez.

O nucleo embute somente `iomanX` e `fileXio`. Codigo que depende de um
driver opcional deve verificar em tempo de execucao se o modulo esta no build
(`athena_module_enabled("memcard")`) em vez de declarar uma dependencia que
obrigaria todo build a inclui-lo.

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

Quando a API depender de um modulo IOP, embuta o driver pelo manifesto
(`embed_irx`), registre-o no IOP manager na primeira vez que for necessario e
faca a API verificar explicitamente se o servico esta disponivel antes de
usa-lo. So drivers exigidos pelo boot pertencem a `src/core/iop_registry.c`.

## 7. Atualize a tipagem e os artefatos por automacao

Apos criar ou alterar o modulo:

1. atualize `<nome>.d.ts`;
2. confirme que o manifesto esta correto;
3. execute `tools/modules.js` conforme o comando definido no projeto;
4. regenere o registro de modulos;
5. regenere o catalogo e os typings agregados;
6. confira o diff dos arquivos gerados.

Quando a implementacao for separada, confirme que `native/<nome>.c` esta em
`sources` e `quickjs/ath_<nome>.c` em `quickjs.sources` no `module.json`.
Compile tambem com `RUNTIME=native` para garantir que a parte nativa nao
depende do QuickJS.
Adicionar apenas o adaptador
QuickJS gera falha de link ou incentiva a reintroducao da regra de negocio no
arquivo errado.

Arquivos como estes sao derivados e nao devem ser editados manualmente:

```text
src/generated/athena_config.h
src/generated/native_registry.c
src/generated/js_registry.c
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
