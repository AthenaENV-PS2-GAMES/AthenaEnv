# Guia de portabilidade de modulos QuickJS

Este documento registra uma falha encontrada durante a migracao do modulo
`System` e define regras para evitar que o mesmo problema apareca em novos
modulos.

## Causa raiz do reset no PS2/PCSX2

O modulo `System` era registrado usando uma tabela de
`JSCFunctionListEntry`. As propriedades `bootPath` e `boot_path` foram
declaradas como getters:

```c
JS_CGETSET_MAGIC_DEF("bootPath", athena_system_get_boot_path, NULL, 0),
JS_CGETSET_MAGIC_DEF("boot_path", athena_system_get_boot_path, NULL, 0),
```

O registro do modulo seguia este fluxo:

```c
JS_NewCModule(ctx, "System", athena_system_module_init);
JS_AddModuleExportList(ctx, m, system_module_funcs, countof(system_module_funcs));
```

e, durante a inicializacao do modulo, os exports eram materializados com:

```c
JS_SetModuleExportList(ctx, m, system_module_funcs, countof(system_module_funcs));
```

Na implementacao do QuickJS usada pelo AthenaEnv, `JS_SetModuleExportList`
suporta funcoes C, strings, inteiros, floats e objetos. O caminho nao trata
entries do tipo getter/setter. Ao encontrar `JS_CGETSET_MAGIC_DEF`, ele cai
no caso nao suportado e executa `abort()`.

No PS2, esse `abort()` nao aparece como uma excecao JavaScript. O efeito
observado e um reset/reboot do ambiente, normalmente logo apos:

```text
[AthenaCore] Evaluating module bootstrap
```

Por isso o problema inicialmente parecia estar no `main.js`, no HostFS ou
na configuracao do PCSX2, embora a falha ocorresse antes da avaliacao do
script da aplicacao.

## Correcao aplicada

As propriedades foram convertidas para exports de string:

```c
JS_PROP_STRING_DEF("bootPath", boot_path, JS_PROP_ENUMERABLE),
JS_PROP_STRING_DEF("boot_path", boot_path, JS_PROP_ENUMERABLE),
```

Essa forma e compativel com `JS_SetModuleExportList` e permite que o
bootstrap conclua normalmente.

O custo dessa escolha e que o valor e capturado quando os exports sao
materializados. Como `boot_path` e definido antes da criacao do runtime,
isso e adequado para o fluxo atual. Se o valor precisar ser dinamico no
futuro, deve ser exposto por uma funcao, por exemplo `System.getBootPath()`,
ou por um objeto/proxy implementado explicitamente, e nao por
`JS_CGETSET_MAGIC_DEF` nessa tabela.

## Regras para novos modulos

### 1. Use somente entries suportadas pelo registro atual

Para tabelas processadas por `JS_SetModuleExportList`, use:

```c
JS_CFUNC_DEF(...)
JS_PROP_STRING_DEF(...)
JS_PROP_INT32_DEF(...)
JS_PROP_INT64_DEF(...)
JS_PROP_FLOAT32_DEF(...)
JS_PROP_FLOAT64_DEF(...)
JS_OBJECT_DEF(...)
```

Antes de usar outro tipo, confirme que ele e tratado em
`src/quickjs/quickjs.c`, na funcao `JS_SetModuleExportList`.

Nao use diretamente nessa tabela, sem adaptar o mecanismo de registro:

```c
JS_CGETSET_DEF(...)
JS_CGETSET_MAGIC_DEF(...)
```

### 2. Mantenha o par de registro consistente

O inicializador do modulo deve seguir o padrao:

```c
static int module_init(JSContext *ctx, JSModuleDef *m) {
    return JS_SetModuleExportList(ctx, m, module_funcs,
                                  countof(module_funcs));
}

JSModuleDef *athena_module_init(JSContext *ctx) {
    return athena_push_module(ctx, module_init, module_funcs,
                              countof(module_funcs), "ModuleName");
}
```

`athena_push_module` usa `JS_AddModuleExportList` para declarar os nomes e
`module_init` usa `JS_SetModuleExportList` para criar os valores. As duas
operacoes precisam usar a mesma tabela e somente entries suportadas por
ambos os caminhos.

### 3. Nao editar o registro gerado manualmente

O modulo deve ser incluido por meio de:

```text
src/modules/<modulo>/module.json
```

Depois, regenere os artefatos usando a automacao existente. O registro
gerado em `src/generated/modules_registry.c` e o bootstrap de imports nao
devem ser editados diretamente.

### 4. Evite efeitos colaterais durante o registro

O inicializador deve apenas:

- criar o modulo;
- declarar os exports;
- materializar os valores;
- retornar o resultado da operacao QuickJS.

Inicializacao de hardware, reset de IOP, loops infinitos e chamadas que
podem abortar o processo devem ficar fora do registro do modulo.

## Checklist de validacao

Para cada modulo portado:

1. Confirme que a tabela de exports usa somente tipos suportados.
2. Compile com `make clean debug`.
3. Verifique se o ELF contem os marcadores do AthenaCore.
4. Execute primeiro um script minimo:

   ```js
   console.log("script started");
   ```

5. Teste o import do modulo sem chamar nenhuma funcao:

   ```js
   import * as Module from "ModuleName";
   console.log("module imported");
   ```

6. Teste cada export individualmente.
7. Execute o mesmo script no PCSX2 e no hardware real.
8. Se ocorrer reset antes de `main.js`, examine nesta ordem:

   ```text
   Starting QuickJS runtime
   Adding QuickJS std helpers
   Evaluating base bootstrap
   Evaluating module bootstrap
   Entry script evaluation returned
   ```

9. Nunca diagnostique um reset apenas pelo retorno ao `OSDSYS`; procure o
   ultimo marcador do AthenaCore.

## Diagnostico rapido no PCSX2

Use o ELF de debug para obter a saida no console do PCSX2:

```bash
make clean debug
```

O fluxo saudavel deve conter:

```text
[AthenaCore] Starting QuickJS runtime...
[AthenaCore] Evaluating base bootstrap
[AthenaCore] Base bootstrap returned 0
[AthenaCore] Evaluating module bootstrap
[AthenaCore] Module bootstrap returned 0
[AthenaCore] Entry script evaluation returned 0
```

Se o ultimo marcador for `Evaluating module bootstrap`, revise primeiro a
tabela de exports de todos os modulos registrados. Um erro nessa fase pode
terminar em `abort()` nativo e nao gerar uma excecao JavaScript.

