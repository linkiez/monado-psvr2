# Próximos passos — PSVR2 Sense

Este documento descreve a sequência recomendada para transformar o rastreamento
óptico experimental em uma implementação validada no SteamVR.

## Estado atual

- O fork está em `https://github.com/linkiez/monado-psvr2`.
- O driver Monado carrega o PSVR2 HMD e os dois controles Sense por Bluetooth.
- O SteamVR ativa os controles esquerdo e direito.
- O modo de câmera `0x10` está ativo e entrega frames de `1040640` bytes.
- O frame `0x10` já é convertido para uma imagem grayscale estéreo de `512x508`
  e encaminhado para `u_psvr2_optical_process_frame`.
- O rastreamento óptico ainda não teve um lock posicional confirmado em hardware.

## Sequência de implementação

### 1. Confirmar detecção dos marcadores

Executar o SteamVR com logs detalhados:

```bash
XRT_LOG=trace PSVR2_LOG=trace steam -applaunch 250820
```

Confirmar a recepção dos frames:

```bash
grep -c "Camera frame - 1040640 bytes" \
  ~/.steam/debian-installation/logs/vrstartup-linux.txt
```

Depois, adicionar temporariamente uma métrica de diagnóstico no rastreador para
registrar:

- quantidade de candidatos encontrados em cada view;
- score máximo dos candidatos;
- quantidade de pares estéreo válidos;
- quantidade de posições publicadas para cada controle.

O diagnóstico deve ser removido ou protegido por uma opção de log antes da
entrega final.

### 2. Criar testes offline do rastreador

Adicionar testes determinísticos para `u_psvr2_optical` sem depender do headset:

- uma imagem estéreo sintética com um marcador conhecido;
- triangulação com baseline e focal conhecidos;
- rejeição de disparity inválida;
- expiração de amostras antigas;
- associação estável entre posições esquerda e direita;
- ausência de candidatos e frames inválidos.

Os testes devem verificar comportamento, não detalhes internos das funções.

### 3. Calibrar a câmera

Substituir os valores aproximados por calibração medida:

- baseline real entre as views;
- focal length em pixels;
- centro óptico;
- distorção das lentes;
- transformação câmera → HMD;
- orientação e posição da câmera em relação ao espaço do HMD.

As opções atuais são:

```text
PSSENSE_OPTICAL_BASELINE_M
PSSENSE_OPTICAL_FOCAL_PX
PSSENSE_OPTICAL_THRESHOLD
```

O valor de `PSSENSE_OPTICAL_THRESHOLD` deve ser ajustado usando frames reais,
com e sem os LEDs dos controles visíveis.

### 4. Validar pose no SteamVR

Com os dois controles ligados:

1. iniciar o SteamVR;
2. confirmar os dois controles no Room Setup;
3. manter o HMD no rosto;
4. mover um controle lentamente em profundidade, lateralmente e verticalmente;
5. confirmar que a posição muda independentemente da orientação;
6. repetir com os controles trocando de lado e com oclusão parcial.

Critérios mínimos:

- a posição deve permanecer estável quando o controle fica parado;
- a posição deve acompanhar movimento em três eixos;
- a orientação IMU deve continuar funcionando quando o marcador é perdido;
- uma amostra expirada não pode deixar uma posição antiga ativa;
- os controles não podem trocar de identidade durante movimento normal.

### 5. Melhorar associação e robustez

Após a validação básica:

- usar histórico de posição para evitar troca de controle;
- rejeitar reflexos isolados e fontes saturadas;
- limitar velocidade máxima plausível;
- suavizar somente posição, sem introduzir atraso excessivo;
- tratar oclusão temporária preservando orientação IMU;
- impedir que um único marcador seja atribuído aos dois controles;
- considerar o stream de LED detector como fonte complementar.

### 6. Avaliar modos de câmera alternativos

O modo `0x10` é necessário para manter o SLAM do HMD e atualmente fornece a
imagem usada pelo rastreador experimental. O modo `0x1` fornece uma imagem
estéreo mais simples, mas pode interromper o fluxo de SLAM; não deve ser usado
como modo padrão sem uma estratégia explícita de troca e recuperação.

O modo `0x4`, documentado como controller-tracking, deve ser investigado apenas
depois que o fluxo `0x10` estiver coberto por testes. A troca de modo precisa
ser validada para não interromper poses do HMD ou deixar transfers USB presos.

### 7. Validar instalação limpa

Repetir a instalação a partir de uma árvore limpa:

```bash
cmake --build build -j"$(nproc)"
sudo cmake --install build
```

Verificar também:

- regra `/etc/udev/rules.d/70-psvr2.rules`;
- `libuvc.so.0` nos dois diretórios do driver Monado do SteamVR;
- `RUNPATH` `$ORIGIN` em `driver_monado.so`;
- `RUNPATH` do `vrmonitor` para as bibliotecas Qt;
- `monado-cli test` detectando HMD e os dois controles.

### 8. Limpar artefatos de desenvolvimento

Antes de uma release ou pull request:

- remover probes temporários de `/tmp`;
- remover logs de diagnóstico persistentes;
- remover dumps de frames que não sejam necessários;
- confirmar que nenhum segredo, token ou credencial foi incluído;
- executar `git diff --check`;
- executar a suíte de testes relevante.

## Critério de conclusão

Considerar o rastreamento óptico concluído somente quando:

1. os testes sintéticos passarem;
2. a calibração produzir erro aceitável em posições conhecidas;
3. os dois controles mantiverem identidade durante movimentos e oclusões;
4. o Room Setup aceitar movimento posicional real;
5. o SteamVR continuar carregando HMD, SLAM, orientação e haptics;
6. o comportamento de perda e recuperação de marcador estiver documentado.

