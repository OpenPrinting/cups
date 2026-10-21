/**
 * CUPS - Root Intelligence Interface Enhancements (Progressive Enhancement)
 * Unix Contemporâneo: Clareza, precisão e autonomia local.
 * 
 * Funcionalidades 100% progressivas (a interface funciona normalmente sem JS):
 * 1. Cópia rápida de comandos CLI com feedback visual
 * 2. Filtragem instantânea de tabelas (impressoras e trabalhos)
 * 3. Atalhos de teclado estilo Unix (/ para busca, ? para ajuda, Esc para fechar)
 * 4. Confirmação explícita de ações destrutivas (com alvo declarado)
 * 5. Assistência de Diagnóstico Local (regras determinísticas + conector opcional offline)
 * 6. Higienização e exportação de resumo técnico para suporte
 */

(function() {
  'use strict';

  // Configurações e estado do diagnóstico local
  const STORAGE_KEY_LLM = 'cups_root_intelligence_llm';
  const DEFAULT_LLM_CONFIG = {
    enabled: false,
    endpoint: 'http://localhost:11434/api/generate',
    model: 'llama3.2',
    timeoutMs: 15000
  };

  /**
   * Base de Conhecimento Determinística Local (Heurísticas Unix CUPS)
   * Regras determinísticas oficiais para diagnóstico de filas e erros
   */
  const HEURISTIC_RULES = [
    {
      id: 'ribbon_media_out',
      match: function(state, msg, uri) {
        const text = (state + ' ' + msg).toLowerCase();
        return text.includes('fita') || text.includes('ribbon') || text.includes('papel') || text.includes('paper') || text.includes('suprimento');
      },
      diagnostic: {
        pt: {
          observed: 'Fila interrompida ou pausada por alerta de suprimento (papel, fita térmica ou etiquetas).',
          origin: 'Sensor de mídia da impressora / Driver de comunicação',
          hypothesis: 'O sensor mecânico ou óptico detectou fim de rolo/bandeja vazia, tampa aberta ou mídia desalinhada.',
          verification: 'Inspecione visualmente a bandeja de alimentação e o painel frontal da impressora física.',
          suggestedAction: 'Abasteça a mídia correta, feche a trava da cabeça térmica e execute o comando para reativar a fila.',
          suggestedCommand: 'cupsenable {queue}',
          commandDesc: 'Reativa o processamento de trabalhos na fila especificada.',
          permission: 'Requer privilégios de administração (membro do grupo lpadmin ou root)'
        },
        en: {
          observed: 'Queue stopped or paused due to supply alert (paper, ribbon, or labels).',
          origin: 'Printer media sensor / Communication driver',
          hypothesis: 'Mechanical or optical sensor detected empty tray/roll, open cover, or misaligned media.',
          verification: 'Visually inspect the physical printer feed tray and front status indicator LEDs.',
          suggestedAction: 'Reload the appropriate media, secure the print head latch, and resume the queue.',
          suggestedCommand: 'cupsenable {queue}',
          commandDesc: 'Resumes processing of jobs on the specified queue.',
          permission: 'Requires administrative privileges (lpadmin group or root)'
        }
      }
    },
    {
      id: 'paused_stopped_state',
      match: function(state, msg, uri) {
        return state === '5' || (state && state.toLowerCase().includes('pausada')) || (state && state.toLowerCase().includes('paused'));
      },
      diagnostic: {
        pt: {
          observed: 'A fila de impressão está atualmente no estado Pausada (State 5).',
          origin: 'CUPS Scheduler (cupsd) ou comando administrativo manual',
          hypothesis: 'A fila foi interrompida manualmente por um administrador ou o CUPS a pausou automaticamente após erro repetido.',
          verification: 'Verifique se há trabalhos travados na fila com `lpstat -o {queue}` e consulte o log de erros.',
          suggestedAction: 'Se não houver problemas pendentes no dispositivo físico, reative a fila para retomar as impressões.',
          suggestedCommand: 'cupsenable {queue}',
          commandDesc: 'Remove a pausa da fila e retoma o envio de trabalhos pendentes.',
          permission: 'Requer privilégios de administração (lpadmin ou sudo)'
        },
        en: {
          observed: 'Print queue is currently in Paused state (State 5).',
          origin: 'CUPS Scheduler (cupsd) or manual administrative action',
          hypothesis: 'Queue was manually stopped by an administrator or automatically paused by cupsd after an error.',
          verification: 'Check for stuck jobs with `lpstat -o {queue}` and inspect error log.',
          suggestedAction: 'If the physical device has no hardware issues, resume the queue.',
          suggestedCommand: 'cupsenable {queue}',
          commandDesc: 'Clears the queue pause and resumes pending print jobs.',
          permission: 'Requires administrative privileges (lpadmin or sudo)'
        }
      }
    },
    {
      id: 'backend_timeout',
      match: function(state, msg, uri) {
        const text = (msg + ' ' + uri).toLowerCase();
        return text.includes('timeout') || text.includes('timed out') || text.includes('host unreachable') || text.includes('falha no backend') || text.includes('connection refused');
      },
      diagnostic: {
        pt: {
          observed: 'Falha de comunicação ou timeout de conexão com o dispositivo de impressão.',
          origin: 'Backend do CUPS (socket, ipp ou usb)',
          hypothesis: 'O endereço de rede da impressora não está acessível (cabo desconectado, IP alterado por DHCP, ou porta bloqueada por firewall).',
          verification: 'Teste a rota de rede com `ping` ou `nc -zv <endereço_ip> 9100`.',
          suggestedAction: 'Confirme o IP fixo no painel da impressora e, se necessário, altere o URI da conexão via painel administrativo.',
          suggestedCommand: 'lpadmin -p {queue} -v socket://<ip_correto>:9100',
          commandDesc: 'Atualiza o endereço de conexão física/rede da fila.',
          permission: 'Requer permissão de administração do CUPS'
        },
        en: {
          observed: 'Communication failure or connection timeout with the printing device.',
          origin: 'CUPS Backend (socket, ipp, or usb)',
          hypothesis: 'The printer network address is unreachable (cable unplugged, IP changed by DHCP, or port blocked).',
          verification: 'Test network route using `ping` or `nc -zv <printer_ip> 9100`.',
          suggestedAction: 'Verify the static IP on the device panel and update the printer URI if necessary.',
          suggestedCommand: 'lpadmin -p {queue} -v socket://<valid_ip>:9100',
          commandDesc: 'Updates the device communication URI for the queue.',
          permission: 'Requires CUPS administration privileges'
        }
      }
    },
    {
      id: 'rejecting_jobs',
      match: function(state, msg, uri, meta) {
        return meta && meta.accepting === '0';
      },
      diagnostic: {
        pt: {
          observed: 'A fila está rejeitando novos trabalhos de impressão.',
          origin: 'Configuração da fila no scheduler CUPS',
          hypothesis: 'A fila foi configurada para manutenção ou desativação temporária usando cupsreject.',
          verification: 'Confirme o status de aceitação executando `lpstat -a {queue}`.',
          suggestedAction: 'Instrua o scheduler a aceitar novos trabalhos novamente usando cupsaccept.',
          suggestedCommand: 'cupsaccept {queue}',
          commandDesc: 'Configura a fila para voltar a aceitar o envio de novos documentos.',
          permission: 'Requer privilégios de administração do CUPS'
        },
        en: {
          observed: 'The queue is currently rejecting new print jobs.',
          origin: 'Queue configuration in CUPS scheduler',
          hypothesis: 'Queue was set to reject jobs during scheduled maintenance or decommissioning using cupsreject.',
          verification: 'Verify acceptance status using `lpstat -a {queue}`.',
          suggestedAction: 'Instruct the scheduler to accept new jobs again using cupsaccept.',
          suggestedCommand: 'cupsaccept {queue}',
          commandDesc: 'Sets the queue to accept incoming print documents.',
          permission: 'Requires CUPS administrative privileges'
        }
      }
    },
    {
      id: 'default_idle_operational',
      match: function(state, msg, uri) {
        return state === '3' || (state && state.toLowerCase().includes('ociosa')) || (state && state.toLowerCase().includes('idle'));
      },
      diagnostic: {
        pt: {
          observed: 'A fila está ociosa e apta a receber trabalhos (State 3 - Idle).',
          origin: 'CUPS Scheduler (cupsd)',
          hypothesis: 'O subsistema local de spooling está operando normalmente. Nota: o estado ocioso do spooler não garante que o hardware remoto esteja ligado ou conectado neste instante.',
          verification: 'Envie uma página de teste para validar o pipeline completo de renderização e envio.',
          suggestedAction: 'Nenhuma ação corretiva necessária. Fila pronta para impressão.',
          suggestedCommand: 'lp -d {queue} /usr/share/cups/data/testprint',
          commandDesc: 'Envia a página de teste oficial do CUPS para a fila.',
          permission: 'Permitido a qualquer usuário do sistema'
        },
        en: {
          observed: 'The queue is idle and ready for incoming jobs (State 3 - Idle).',
          origin: 'CUPS Scheduler (cupsd)',
          hypothesis: 'Local spooling subsystem is functioning normally. Note: an idle spooler state does not guarantee physical hardware connectivity at this exact moment.',
          verification: 'Submit a test page to validate the complete rendering and transfer pipeline.',
          suggestedAction: 'No corrective action needed. Queue is ready to print.',
          suggestedCommand: 'lp -d {queue} /usr/share/cups/data/testprint',
          commandDesc: 'Submits the standard CUPS test page to the queue.',
          permission: 'Allowed for any system user'
        }
      }
    }
  ];

  /**
   * Inicialização quando o DOM estiver pronto
   */
  document.addEventListener('DOMContentLoaded', function() {
    initModeToggle();
    initClipboard();
    initTableSearch();
    initKeyboardShortcuts();
    initDestructiveConfirmations();
    initDiagnosticAssistants();
    initOnboardingAndTour();
    initCommandPalette();

    // Deep linking & Atalhos de Inicialização Direta via URL/Hash
    if (window.location.hash === '#cmd-palette' || window.location.search.includes('palette=1')) {
      setTimeout(openCommandPalette, 120);
    } else if (window.location.hash === '#tour' || window.location.search.includes('tour=1')) {
      setTimeout(openTourModal, 120);
    }
  });

  /**
   * 1. Cópia de Comandos para a Área de Transferência
   */
  function initClipboard() {
    const blocks = document.querySelectorAll('.cli-block, pre.command');
    blocks.forEach(function(block) {
      if (block.querySelector('.copy-btn')) return;

      const btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'copy-btn';
      btn.setAttribute('aria-label', 'Copiar comando');
      btn.innerHTML = '<span class="copy-icon">⎘</span> <span class="copy-text">Copiar</span>';

      btn.addEventListener('click', function(e) {
        e.preventDefault();
        e.stopPropagation();

        // Extrai texto ignorando o prefixo do prompt '$ '
        let rawText = block.innerText || block.textContent;
        // Remove rótulo do botão caso capturado
        rawText = rawText.replace(/Copiar|Copiado!|⎘/g, '').trim();
        // Remove prefixo '$ ' no início de linhas
        const cleanCmd = rawText.replace(/^\$\s*/gm, '').trim();

        copyTextToClipboard(cleanCmd, function(ok) {
          if (ok) {
            btn.classList.add('copied');
            btn.innerHTML = '<span class="copy-icon">✓</span> <span class="copy-text">Copiado!</span>';
            const isPt = document.documentElement.lang ? document.documentElement.lang.startsWith('pt') : true;
            showToast(isPt ? 'Comando copiado para a área de transferência!' : 'Command copied to clipboard!');
            setTimeout(function() {
              btn.classList.remove('copied');
              btn.innerHTML = '<span class="copy-icon">⎘</span> <span class="copy-text">Copiar</span>';
            }, 2000);
          }
        });
      });

      // Se for pre.command, posiciona relativo
      if (window.getComputedStyle(block).position === 'static') {
        block.style.position = 'relative';
      }
      block.appendChild(btn);
    });
  }

  function showToast(message, icon) {
    icon = icon || '✓';
    let existing = document.querySelector('.cups-toast');
    if (existing) existing.remove();

    const toast = document.createElement('div');
    toast.className = 'cups-toast';
    toast.setAttribute('role', 'status');
    toast.innerHTML = `<span class="cups-toast-icon">${icon}</span> <span>${message}</span>`;
    document.body.appendChild(toast);

    setTimeout(function() {
      toast.style.transition = 'opacity 200ms ease, transform 200ms ease';
      toast.style.opacity = '0';
      toast.style.transform = 'translateY(8px)';
      setTimeout(function() {
        if (toast.parentNode) toast.parentNode.removeChild(toast);
      }, 200);
    }, 2400);
  }

  function copyTextToClipboard(text, callback) {
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).then(function() {
        callback(true);
      }).catch(function() {
        fallbackCopy(text, callback);
      });
    } else {
      fallbackCopy(text, callback);
    }
  }

  function fallbackCopy(text, callback) {
    const ta = document.createElement('textarea');
    ta.value = text;
    ta.style.position = 'fixed';
    ta.style.opacity = '0';
    document.body.appendChild(ta);
    ta.select();
    try {
      const successful = document.execCommand('copy');
      document.body.removeChild(ta);
      callback(successful);
    } catch (err) {
      document.body.removeChild(ta);
      callback(false);
    }
  }

  /**
   * 2. Filtragem Instantânea de Tabelas (Printers e Jobs)
   */
  function initTableSearch() {
    const searchInputs = document.querySelectorAll('input[type="search"][name="QUERY"]');
    const tables = document.querySelectorAll('table.list');

    if (!tables.length || !searchInputs.length) return;

    searchInputs.forEach(function(input) {
      // Se já houver tabela, adiciona filtragem progressiva no evento input
      input.addEventListener('input', function() {
        const query = input.value.trim().toLowerCase();
        tables.forEach(function(table) {
          filterTableRows(table, query);
        });
      });
    });
  }

  function filterTableRows(table, query) {
    const tbody = table.querySelector('tbody') || table;
    const rows = tbody.querySelectorAll('tr:not(.no-filter)');
    let visibleCount = 0;

    rows.forEach(function(row) {
      const text = (row.textContent || row.innerText).toLowerCase();
      if (!query || text.includes(query)) {
        row.style.display = '';
        visibleCount++;
      } else {
        row.style.display = 'none';
      }
    });

    // Exibe ou oculta estado vazio customizado caso 0 itens
    let emptyRow = table.querySelector('.table-empty-row');
    if (visibleCount === 0 && query) {
      if (!emptyRow) {
        emptyRow = document.createElement('tr');
        emptyRow.className = 'table-empty-row no-filter';
        const cols = table.querySelectorAll('th').length || 5;
        emptyRow.innerHTML = `<td colspan="${cols}" class="empty-state-cell">
          <div class="empty-ascii">[ ! ]</div>
          <div class="empty-text">Nenhum registro corresponde ao filtro <code>"${escapeHtml(query)}"</code></div>
        </td>`;
        tbody.appendChild(emptyRow);
      }
      emptyRow.style.display = '';
    } else if (emptyRow) {
      emptyRow.style.display = 'none';
    }
  }

  /**
   * 3. Atalhos de Teclado Estilo Unix
   */
  function initKeyboardShortcuts() {
    document.addEventListener('keydown', function(e) {
      // Se for atalho global Ctrl+K ou Cmd+K, intercepta mesmo dentro de inputs
      if ((e.ctrlKey || e.metaKey) && (e.key === 'k' || e.key === 'K')) {
        e.preventDefault();
        toggleCommandPalette();
        return;
      }

      // Se estiver digitando em um input ou textarea, não interceptar a menos que seja Esc
      const activeTag = document.activeElement ? document.activeElement.tagName.toLowerCase() : '';
      const isInput = activeTag === 'input' || activeTag === 'textarea' || activeTag === 'select';

      if (e.key === 'Escape') {
        // Fecha modais e desfoque de inputs
        if (isInput) {
          document.activeElement.blur();
        }
        closeHelpModal();
        closeSettingsModal();
        closeCommandPalette();
        closeTourModal();
        return;
      }

      if (isInput) return;

      if (e.key === '/') {
        // Foca no campo de busca principal
        const searchInput = document.querySelector('input[type="search"]');
        if (searchInput) {
          e.preventDefault();
          searchInput.focus();
          searchInput.select();
        }
      } else if (e.key === '?') {
        // Abre painel de ajuda/atalhos
        e.preventDefault();
        toggleHelpModal();
      }
    });
  }

  function toggleHelpModal() {
    let modal = document.getElementById('cups-shortcuts-modal');
    if (modal && modal.classList.contains('active')) {
      closeHelpModal();
    } else {
      openHelpModal();
    }
  }

  function openHelpModal() {
    let modal = document.getElementById('cups-shortcuts-modal');
    if (!modal) {
      modal = document.createElement('div');
      modal.id = 'cups-shortcuts-modal';
      modal.className = 'cups-modal-overlay';
      modal.innerHTML = `
        <div class="cups-modal-box" role="dialog" aria-modal="true" aria-labelledby="modal-title">
          <div class="modal-header">
            <span class="prompt-marker">&gt;_</span>
            <h3 id="modal-title">Atalhos de Terminal & Sistema</h3>
            <button type="button" class="modal-close-btn" aria-label="Fechar modal">&times;</button>
          </div>
          <div class="modal-body">
            <table class="shortcuts-table">
              <tr><th><kbd>/</kbd></th><td>Focar campo de pesquisa</td></tr>
              <tr><th><kbd>?</kbd></th><td>Exibir esta tela de atalhos</td></tr>
              <tr><th><kbd>Esc</kbd></th><td>Fechar janelas e desmarcar seleção</td></tr>
              <tr><th><kbd>Tab</kbd></th><td>Navegar ordenadamente pelos elementos interativos</td></tr>
            </table>
            <div class="modal-section-title">Comandos de Linha de Comando Úteis</div>
            <pre class="cli-block"><span class="cmd-prompt">$</span> lpstat -p -d <span class="cmd-comment"># Listar status de impressoras e fila padrão</span></pre>
            <pre class="cli-block"><span class="cmd-prompt">$</span> lpstat -o <span class="cmd-comment"># Listar trabalhos em processamento e pendentes</span></pre>
            <pre class="cli-block"><span class="cmd-prompt">$</span> cupsenable &lt;fila&gt; <span class="cmd-comment"># Reativar fila de impressão pausada</span></pre>
            <pre class="cli-block"><span class="cmd-prompt">$</span> cancel &lt;id_trabalho&gt; <span class="cmd-comment"># Cancelar um trabalho de impressão específico</span></pre>
          </div>
        </div>
      `;
      document.body.appendChild(modal);

      modal.querySelector('.modal-close-btn').addEventListener('click', closeHelpModal);
      modal.addEventListener('click', function(e) {
        if (e.target === modal) closeHelpModal();
      });
      initClipboard();
    }
    modal.classList.add('active');
  }

  function closeHelpModal() {
    const modal = document.getElementById('cups-shortcuts-modal');
    if (modal) modal.classList.remove('active');
  }

  function closeSettingsModal() {
    const modal = document.getElementById('cups-ai-settings-modal');
    if (modal) modal.classList.remove('active');
  }

  /**
   * 4. Confirmação Proporcional de Ações Destrutivas com Alvo Explícito
   */
  function initDestructiveConfirmations() {
    document.querySelectorAll('form').forEach(function(form) {
      const opInput = form.querySelector('input[name="OP"], select[name="OP"]');
      const submitBtn = form.querySelector('input[type="submit"]');

      form.addEventListener('submit', function(e) {
        const opVal = opInput ? (opInput.value || '') : '';
        const btnVal = submitBtn ? (submitBtn.value || '') : '';

        const isCancelJob = opVal === 'cancel-job' || btnVal.toLowerCase().includes('cancel') || btnVal.toLowerCase().includes('cancelar');
        const isCancelAll = opVal === 'cancel-jobs' || btnVal.toLowerCase().includes('cancel all') || btnVal.toLowerCase().includes('cancelar todos');
        const isDeletePrinter = opVal === 'delete-printer' || opVal === 'delete-class' || btnVal.toLowerCase().includes('excluir') || btnVal.toLowerCase().includes('delete');

        if (isCancelJob || isCancelAll || isDeletePrinter) {
          const jobId = form.querySelector('input[name="job_id"]')?.value;
          const printerName = form.querySelector('input[name="printer_name"]')?.value || document.querySelector('h1.title')?.innerText || 'alvo';

          let message = '';
          if (isCancelAll) {
            message = `Atenção: Deseja realmente CANCELAR TODOS os trabalhos da fila "${printerName}"?\nEssa ação é irreversível.`;
          } else if (isCancelJob && jobId) {
            message = `Confirma o cancelamento do trabalho #${jobId} na fila "${printerName}"?`;
          } else if (isDeletePrinter) {
            message = `Atenção crítica: Deseja realmente EXCLUIR permanentemente a fila "${printerName}" do servidor CUPS?`;
          }

          if (message && !window.confirm(message)) {
            e.preventDefault();
          }
        }
      });
    });
  }

  /**
   * 5. Assistência de Diagnóstico ("Root Intelligence" Contextual)
   */
  function initDiagnosticAssistants() {
    const container = document.querySelector('.diagnostic-assistant-mount');
    if (!container) return;

    // Detecta contexto da página (impressora, estado, mensagem)
    const queueName = container.getAttribute('data-queue') || '';
    const stateCode = container.getAttribute('data-state') || '';
    const stateMsg = container.getAttribute('data-message') || '';
    const deviceUri = container.getAttribute('data-uri') || '';
    const isAccepting = container.getAttribute('data-accepting') || '1';
    const lang = document.documentElement.lang === 'en' ? 'en' : 'pt';

    // Procura diagnósticos correspondentes na base determinística
    const matchedRule = HEURISTIC_RULES.find(function(rule) {
      return rule.match(stateCode, stateMsg, deviceUri, { accepting: isAccepting });
    }) || HEURISTIC_RULES[HEURISTIC_RULES.length - 1]; // Fallback para idle/genérico

    const diag = matchedRule.diagnostic[lang];
    const formattedCmd = diag.suggestedCommand.replace('{queue}', queueName || 'printer');

    renderDiagnosticUI(container, {
      queueName: queueName,
      stateCode: stateCode,
      stateMsg: stateMsg,
      ruleId: matchedRule.id,
      diagnostic: diag,
      formattedCmd: formattedCmd,
      lang: lang
    });
  }

  function renderDiagnosticUI(container, data) {
    const diag = data.diagnostic;
    const isPt = data.lang === 'pt';

    const llmConfig = getLLMConfig();
    const isLLMActive = llmConfig.enabled;

    container.innerHTML = `
      <div class="diagnostic-panel" role="region" aria-labelledby="diag-panel-title">
        <div class="diag-header">
          <div class="diag-header-left">
            <span class="prompt-marker">&gt;_</span>
            <span id="diag-panel-title" class="diag-title">${isPt ? 'Assistência de Diagnóstico' : 'Diagnostic Assistant'}</span>
            <span class="diag-badge-rule">[${isPt ? 'Regra Determinística Local' : 'Local Deterministic Rule'}]</span>
          </div>
          <div class="diag-header-right">
            <button type="button" class="btn-diag-action" id="btn-copy-support-summary" title="${isPt ? 'Copiar resumo técnico higienizado para abertura de chamado' : 'Copy sanitized technical summary for support ticket'}">
              ⎘ ${isPt ? 'Copiar Resumo de Suporte' : 'Copy Support Summary'}
            </button>
            <button type="button" class="btn-diag-action btn-config-ai" id="btn-open-ai-settings" title="${isPt ? 'Configurar inferência local opcional (Ollama/Offline)' : 'Configure optional local inference (Ollama/Offline)'}">
              ⚙ ${isPt ? 'Configurar IA Local' : 'Configure Local AI'}
            </button>
          </div>
        </div>

        <div class="diag-body">
          <div class="diag-grid">
            <div class="diag-card diag-observed">
              <div class="card-tag tag-obs">${isPt ? 'Observado' : 'Observed'}</div>
              <div class="card-content">
                <p><strong>${escapeHtml(diag.observed)}</strong></p>
                ${data.stateMsg ? `<p class="diag-msg-quote"><code>"${escapeHtml(data.stateMsg)}"</code></p>` : ''}
                <div class="card-meta">
                  <span>${isPt ? 'Origem' : 'Origin'}: ${escapeHtml(diag.origin)}</span>
                  <span>${isPt ? 'Horário' : 'Timestamp'}: ${new Date().toLocaleTimeString()}</span>
                </div>
              </div>
            </div>

            <div class="diag-card diag-hypothesis">
              <div class="card-tag tag-hyp">${isPt ? 'Hipótese' : 'Hypothesis'}</div>
              <div class="card-content">
                <p>${escapeHtml(diag.hypothesis)}</p>
                <div class="card-meta">
                  <span class="hypothesis-note">${isPt ? 'Interpretação inferida a partir dos sinais coletados' : 'Inferred interpretation based on collected signals'}</span>
                </div>
              </div>
            </div>

            <div class="diag-card diag-verification">
              <div class="card-tag tag-ver">${isPt ? 'Verificação' : 'Verification'}</div>
              <div class="card-content">
                <p>${escapeHtml(diag.verification)}</p>
                <div class="card-meta">
                  <span>${isPt ? 'Próximo passo para confirmar o diagnóstico' : 'Next step to confirm diagnosis'}</span>
                </div>
              </div>
            </div>

            <div class="diag-card diag-action">
              <div class="card-tag tag-act">${isPt ? 'Ação Sugerida' : 'Suggested Action'}</div>
              <div class="card-content">
                <p>${escapeHtml(diag.suggestedAction)}</p>
                <pre class="cli-block"><span class="cmd-prompt">$</span> ${escapeHtml(data.formattedCmd)}</pre>
                <div class="card-meta">
                  <span>${escapeHtml(diag.permission)}</span>
                </div>
              </div>
            </div>
          </div>

          <!-- Área Opcional de Inferência Local (Ollama) -->
          <div class="diag-llm-section" id="diag-llm-section">
            <div class="llm-header">
              <span class="llm-status-indicator ${isLLMActive ? 'status-configured' : 'status-unconfigured'}">●</span>
              <span class="llm-status-text">
                ${isLLMActive ? 
                  `${isPt ? 'IA Local Conectada' : 'Local AI Connected'} (${escapeHtml(llmConfig.model)})` : 
                  `${isPt ? 'IA Externa / LLM: Não configurada (Operando 100% via regras locais)' : 'External AI / LLM: Not configured (Operating 100% on local rules)'}`
                }
              </span>
              ${isLLMActive ? `
                <button type="button" class="btn-diag-action" id="btn-run-llm-query">
                  ${isPt ? 'Consultar Análise Profunda Local' : 'Run Local Deep Analysis'}
                </button>
              ` : `
                <span class="llm-offline-note">[${isPt ? 'Controle Local: Zero dados enviados à internet' : 'Local Control: Zero data sent to internet'}]</span>
              `}
            </div>
            <div class="llm-result-box" id="llm-result-box" style="display: none;"></div>
          </div>
        </div>
      </div>
    `;

    // Conectar eventos
    const copySummaryBtn = container.querySelector('#btn-copy-support-summary');
    if (copySummaryBtn) {
      copySummaryBtn.addEventListener('click', function() {
        generateSupportSummary(data);
      });
    }

    const configAIBtn = container.querySelector('#btn-open-ai-settings');
    if (configAIBtn) {
      configAIBtn.addEventListener('click', function() {
        openAISettingsModal(data);
      });
    }

    const runLLMBtn = container.querySelector('#btn-run-llm-query');
    if (runLLMBtn) {
      runLLMBtn.addEventListener('click', function() {
        queryLocalLLM(data);
      });
    }

    initClipboard();
  }

  /**
   * 6. Higienização e Geração de Resumo Técnico de Suporte
   */
  function generateSupportSummary(data) {
    const isPt = data.lang === 'pt';
    const diag = data.diagnostic;

    const summaryText = [
      `==================================================`,
      `RELATÓRIO TÉCNICO DE DIAGNÓSTICO CUPS - ROOT INTEL`,
      `Data/Hora: ${new Date().toISOString()}`,
      `Servidor: ${window.location.hostname || 'cups-host'}`,
      `Fila de Impressão: ${data.queueName || 'Global'}`,
      `Código de Estado: ${data.stateCode}`,
      `Mensagem do Dispositivo: ${data.stateMsg ? '"' + data.stateMsg + '"' : '(nenhuma mensagem)'}`,
      `--------------------------------------------------`,
      `[1] OBSERVADO:`,
      `  ${diag.observed}`,
      ``,
      `[2] HIPÓTESE:`,
      `  ${diag.hypothesis}`,
      ``,
      `[3] VERIFICAÇÃO RECOMENDADA:`,
      `  ${diag.verification}`,
      ``,
      `[4] AÇÃO SUGERIDA / COMANDO:`,
      `  ${diag.suggestedAction}`,
      `  Comando: $ ${data.formattedCmd}`,
      `  Permissões: ${diag.permission}`,
      `==================================================`
    ].join('\n');

    copyTextToClipboard(summaryText, function(ok) {
      if (ok) {
        showToast(isPt ? 
          'Resumo de suporte higienizado copiado com sucesso!' : 
          'Sanitized support summary copied to clipboard!');
      } else {
        alert(summaryText);
      }
    });
  }

  /**
   * 7. Configurações da IA Local (Ollama / Localhost)
   */
  function getLLMConfig() {
    try {
      const stored = localStorage.getItem(STORAGE_KEY_LLM);
      if (stored) {
        return Object.assign({}, DEFAULT_LLM_CONFIG, JSON.parse(stored));
      }
    } catch (e) {}
    return DEFAULT_LLM_CONFIG;
  }

  function saveLLMConfig(config) {
    try {
      localStorage.setItem(STORAGE_KEY_LLM, JSON.stringify(config));
    } catch (e) {}
  }

  function openAISettingsModal(currentData) {
    let modal = document.getElementById('cups-ai-settings-modal');
    const isPt = currentData.lang === 'pt';
    const cfg = getLLMConfig();

    if (!modal) {
      modal = document.createElement('div');
      modal.id = 'cups-ai-settings-modal';
      modal.className = 'cups-modal-overlay';
      document.body.appendChild(modal);
    }

    modal.innerHTML = `
      <div class="cups-modal-box" role="dialog" aria-modal="true" aria-labelledby="ai-settings-title">
        <div class="modal-header">
          <span class="prompt-marker">&gt;_</span>
          <h3 id="ai-settings-title">${isPt ? 'Configuração da Assistência de IA Local' : 'Local AI Assistant Configuration'}</h3>
          <button type="button" class="modal-close-btn" aria-label="Fechar">&times;</button>
        </div>
        <div class="modal-body">
          <p class="modal-desc">
            ${isPt ? 
              'A filosofia do CUPS Root Intelligence prioriza o processamento <strong>100% local, autônomo e offline</strong>. O adaptador de IA conecta-se diretamente a instâncias locais (como Ollama ou Llama.cpp) rodando no seu ambiente. Nenhum conteúdo de documento, senha ou token é enviado para servidores externos.' : 
              'CUPS Root Intelligence strictly prioritizes <strong>100% local, autonomous, and offline</strong> execution. The AI adapter connects directly to local runtimes (like Ollama or Llama.cpp) running on your host. No document contents, passwords, or tokens are ever sent to remote clouds.'
            }
          </p>

          <form id="ai-settings-form">
            <div class="form-group">
              <label>
                <input type="checkbox" id="ai-enabled-chk" ${cfg.enabled ? 'checked' : ''}>
                <strong>${isPt ? 'Habilitar conector de IA local (Ollama / Runtime compatível)' : 'Enable local AI connector (Ollama / Compatible runtime)'}</strong>
              </label>
            </div>

            <div class="form-group">
              <label for="ai-endpoint-input">${isPt ? 'Endpoint local de inferência' : 'Local inference endpoint'}:</label>
              <input type="text" id="ai-endpoint-input" class="mono-input" value="${escapeHtml(cfg.endpoint)}" placeholder="http://localhost:11434/api/generate">
              <small class="form-help">${isPt ? 'Exemplos: Ollama (http://localhost:11434/api/generate) ou LM Studio (http://localhost:1234/v1/chat/completions)' : 'Examples: Ollama (http://localhost:11434/api/generate) or LM Studio (http://localhost:1234/v1/chat/completions)'}</small>
            </div>

            <div class="form-group">
              <label for="ai-model-input">${isPt ? 'Modelo local instalado' : 'Installed local model'}:</label>
              <input type="text" id="ai-model-input" class="mono-input" value="${escapeHtml(cfg.model)}" placeholder="llama3.2">
              <small class="form-help">${isPt ? 'Nome do modelo baixado localmente (ex: llama3.2, mistral, deepseek-r1:8b)' : 'Locally downloaded model name (e.g. llama3.2, mistral, deepseek-r1:8b)'}</small>
            </div>

            <div class="modal-actions">
              <button type="submit" class="btn-primary">${isPt ? 'Salvar Configuração' : 'Save Configuration'}</button>
              <button type="button" class="btn-secondary" id="btn-ai-modal-cancel">${isPt ? 'Cancelar' : 'Cancel'}</button>
            </div>
          </form>
        </div>
      </div>
    `;

    modal.classList.add('active');

    modal.querySelector('.modal-close-btn').addEventListener('click', closeSettingsModal);
    modal.querySelector('#btn-ai-modal-cancel').addEventListener('click', closeSettingsModal);
    modal.addEventListener('click', function(e) {
      if (e.target === modal) closeSettingsModal();
    });

    modal.querySelector('#ai-settings-form').addEventListener('submit', function(e) {
      e.preventDefault();
      const updated = {
        enabled: modal.querySelector('#ai-enabled-chk').checked,
        endpoint: modal.querySelector('#ai-endpoint-input').value.trim() || DEFAULT_LLM_CONFIG.endpoint,
        model: modal.querySelector('#ai-model-input').value.trim() || DEFAULT_LLM_CONFIG.model,
        timeoutMs: 15000
      };
      saveLLMConfig(updated);
      closeSettingsModal();
      initDiagnosticAssistants();
      showToast(isPt ? 'Configuração de IA local salva com sucesso!' : 'Local AI configuration saved successfully!');
    });
  }

  /**
   * Consulta à IA Local com Tratamento de Erros e Timeouts Estritos
   */
  function queryLocalLLM(data) {
    const isPt = data.lang === 'pt';
    const cfg = getLLMConfig();
    const resultBox = document.getElementById('llm-result-box');
    if (!resultBox) return;

    resultBox.style.display = 'block';
    resultBox.innerHTML = `
      <div class="llm-loading">
        <span class="loading-spinner">⌛</span>
        <span>${isPt ? 'Consultando modelo local em ' + escapeHtml(cfg.endpoint) + '...' : 'Querying local model at ' + escapeHtml(cfg.endpoint) + '...'}</span>
      </div>
    `;

    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), cfg.timeoutMs);

    // Prompt estritamente estruturado e higienizado
    const sanitizedPrompt = `Você é o consultor de diagnóstico técnico Unix para o servidor CUPS.
Analise as seguintes evidências reais da fila de impressão:
Fila: ${data.queueName}
Código de Estado: ${data.stateCode}
Mensagem de Erro: ${data.stateMsg || 'Nenhuma'}

Responda em texto claro e direto, estruturado em exatamente 4 tópicos:
1. Observado
2. Hipótese
3. Verificação (próximo passo com comando)
4. Ação sugerida (com comando e privilégio)`;

    fetch(cfg.endpoint, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        model: cfg.model,
        prompt: sanitizedPrompt,
        stream: false
      }),
      signal: controller.signal
    })
    .then(res => {
      clearTimeout(timeoutId);
      if (!res.ok) throw new Error('HTTP ' + res.status);
      return res.json();
    })
    .then(json => {
      const responseText = json.response || json.message?.content || JSON.stringify(json);
      resultBox.innerHTML = `
        <div class="llm-response-success">
          <div class="llm-response-header">
            <span class="tag-ai-badge">[${isPt ? 'Resposta da IA Local Conectada: ' + escapeHtml(cfg.model) : 'Connected Local AI Response: ' + escapeHtml(cfg.model)}]</span>
          </div>
          <div class="llm-response-text">${escapeHtml(responseText)}</div>
        </div>
      `;
    })
    .catch(err => {
      clearTimeout(timeoutId);
      const isTimeout = err.name === 'AbortError';
      const errMsg = isTimeout ? 
        (isPt ? 'Tempo excedido ao aguardar resposta do modelo local (Timeout de 15s).' : 'Connection timed out waiting for local model (15s timeout).') :
        (isPt ? 'Falha de conexão com o runtime local (' + err.message + '). Verifique se o Ollama está em execução.' : 'Connection failure with local runtime (' + err.message + '). Verify Ollama is running.');

      resultBox.innerHTML = `
        <div class="llm-response-fallback">
          <div class="llm-fallback-header">
            <span class="status-error">✕ ${errMsg}</span>
          </div>
          <p class="fallback-explanation">
            ${isPt ? 
              'O sistema permanece 100% operacional utilizando a <strong>Base de Regras Determinísticas Local</strong> exibida acima.' : 
              'The system remains 100% operational using the <strong>Local Deterministic Rules Engine</strong> displayed above.'
            }
          </p>
        </div>
      `;
    });
  }

  /**
   * 7. Gerenciador de Modo de Usuário (Iniciante vs Avançado)
   */
  const STORAGE_KEY_MODE = 'cups_user_mode';

  function initModeToggle() {
    let savedMode = localStorage.getItem(STORAGE_KEY_MODE);
    if (window.location.search.includes('mode=advanced')) {
      savedMode = 'advanced';
    } else if (window.location.search.includes('mode=beginner')) {
      savedMode = 'beginner';
    } else if (!savedMode || (savedMode !== 'beginner' && savedMode !== 'advanced')) {
      savedMode = 'beginner'; // Padrão amigável e acolhedor para iniciantes
    }

    applyUserMode(savedMode, false);

    const toggleBtn = document.getElementById('mode-toggle-btn');
    if (toggleBtn) {
      toggleBtn.addEventListener('click', function(e) {
        e.preventDefault();
        const currentMode = document.documentElement.classList.contains('mode-advanced') ? 'advanced' : 'beginner';
        const newMode = currentMode === 'beginner' ? 'advanced' : 'beginner';
        applyUserMode(newMode, true);
      });
    }
  }

  function applyUserMode(mode, showFeedback) {
    const isPt = document.documentElement.lang ? document.documentElement.lang.startsWith('pt') : true;
    localStorage.setItem(STORAGE_KEY_MODE, mode);

    if (mode === 'advanced') {
      document.documentElement.classList.remove('mode-beginner');
      document.documentElement.classList.add('mode-advanced');
    } else {
      document.documentElement.classList.remove('mode-advanced');
      document.documentElement.classList.add('mode-beginner');
    }

    const toggleBtn = document.getElementById('mode-toggle-btn');
    if (toggleBtn) {
      const labelSpan = toggleBtn.querySelector('.mode-label-text');
      if (labelSpan) {
        if (mode === 'advanced') {
          labelSpan.textContent = isPt ? 'Modo: Avançado' : 'Mode: Advanced';
          toggleBtn.title = isPt ? 'Clique para alternar para o Modo Iniciante' : 'Click to switch to Beginner Mode';
        } else {
          labelSpan.textContent = isPt ? 'Modo: Iniciante' : 'Mode: Beginner';
          toggleBtn.title = isPt ? 'Clique para alternar para o Modo Avançado' : 'Click to switch to Advanced Mode';
        }
      }
    }

    if (showFeedback) {
      if (mode === 'advanced') {
        showToast(isPt ? 'Modo Avançado ativado: Métricas brutas e terminal habilitados.' : 'Advanced Mode activated: Raw telemetry and terminal tools enabled.', '⚡');
      } else {
        showToast(isPt ? 'Modo Iniciante ativado: Guias práticos e checklist visíveis.' : 'Beginner Mode activated: Guides and onboarding checklist visible.', '🌱');
      }
    }
  }

  /**
   * 8. Checklist de Integração Onboarding & Tour Guiado Interativo
   */
  const STORAGE_KEY_ONBOARDING = 'cups_onboarding_tasks';

  function initOnboardingAndTour() {
    const isPt = document.documentElement.lang ? document.documentElement.lang.startsWith('pt') : true;
    const banner = document.getElementById('onboarding-banner');
    if (!banner) return;

    let savedTasks = {};
    try {
      savedTasks = JSON.parse(localStorage.getItem(STORAGE_KEY_ONBOARDING) || '{}');
    } catch(e) {
      savedTasks = {};
    }

    const checkItems = banner.querySelectorAll('.check-item');
    checkItems.forEach(function(item) {
      const taskId = item.getAttribute('data-task');
      const chk = item.querySelector('input[type="checkbox"]');
      const box = item.querySelector('.check-box');

      if (taskId && savedTasks[taskId] !== undefined) {
        chk.checked = !!savedTasks[taskId];
      }

      function updateItemUI() {
        if (chk.checked) {
          item.classList.add('done');
          box.textContent = '✓';
        } else {
          item.classList.remove('done');
          box.textContent = '';
        }
      }

      updateItemUI();

      item.addEventListener('click', function(e) {
        if (e.target.tagName && e.target.tagName.toLowerCase() === 'a') return;
        e.preventDefault();
        chk.checked = !chk.checked;
        savedTasks[taskId] = chk.checked;
        localStorage.setItem(STORAGE_KEY_ONBOARDING, JSON.stringify(savedTasks));
        updateItemUI();
        updateProgress();
        if (chk.checked) {
          showToast(isPt ? 'Etapa marcada como concluída!' : 'Step marked as completed!');
        }
      });
    });

    function updateProgress() {
      const total = checkItems.length || 3;
      let completed = 0;
      checkItems.forEach(function(item) {
        const chk = item.querySelector('input[type="checkbox"]');
        if (chk && chk.checked) completed++;
      });
      const pct = Math.round((completed / total) * 100);
      const fill = document.getElementById('onboarding-progress-fill');
      const text = document.getElementById('onboarding-progress-text');
      if (fill) fill.style.width = pct + '%';
      if (text) {
        text.textContent = isPt ? `${completed} de ${total} concluído (${pct}%)` : `${completed} of ${total} completed (${pct}%)`;
      }
    }

    updateProgress();

    const tourBtn = document.getElementById('btn-start-tour');
    if (tourBtn) {
      tourBtn.addEventListener('click', function(e) {
        e.preventDefault();
        openTourModal();
      });
    }
  }

  /**
   * Modal de Tour Guiado (3 Passos)
   */
  function openTourModal() {
    const isPt = document.documentElement.lang ? document.documentElement.lang.startsWith('pt') : true;
    closeCommandPalette();
    closeHelpModal();
    closeSettingsModal();

    let tourModal = document.getElementById('cups-tour-modal');
    if (tourModal) tourModal.remove();

    const steps = [
      {
        badge: isPt ? 'Passo 1 de 3: Arquitetura' : 'Step 1 of 3: Architecture',
        title: isPt ? 'O Coração do Linux Printing' : 'The Core of Linux Printing',
        desc: isPt ? 
          'O CUPS orquestra o spooler local, filas de impressão e o pipeline de rasterização. Com o padrão <strong>IPP Everywhere™</strong>, você conecta impressoras de rede sem depender de drivers proprietários ou bloatware.' : 
          'CUPS orchestrates the local spooler, queues, and rasterization pipeline. With <strong>IPP Everywhere™</strong>, network printers work driverless out-of-the-box.',
        features: [
          isPt ? '✓ Impressão direta baseada em padrões abertos' : '✓ Direct printing based on open standards',
          isPt ? '✓ Segurança com autenticação local e sid CSRF tokens' : '✓ Local security with sid CSRF tokens',
          isPt ? '✓ 100% autônomo e sem dependências de nuvem externa' : '✓ 100% autonomous with zero external cloud dependencies'
        ]
      },
      {
        badge: isPt ? 'Passo 2 de 3: Diagnóstico' : 'Step 2 of 3: Diagnostics',
        title: isPt ? 'Root Intelligence & Resolução Rápida' : 'Root Intelligence & Fast Remediation',
        desc: isPt ? 
          'Fitas térmicas, papel e falhas de rede são diagnosticadas instantaneamente em 4 camadas (Observado, Hipótese, Verificação e Ação) com comandos de terminal prontos para copiar.' : 
          'Media out, ribbon faults, and socket timeouts are diagnosed instantly in 4 clear layers with copy-ready terminal commands and permission indicators.',
        features: [
          isPt ? '✓ Base de regras determinísticas oficial e local' : '✓ Deterministic local knowledge base',
          isPt ? '✓ Conexão opcional com IA local (Ollama offline)' : '✓ Optional connection to local AI (Ollama offline)',
          isPt ? '✓ Cópia de relatórios higienizados para equipes de suporte' : '✓ Sanitized support summary copy button'
        ]
      },
      {
        badge: isPt ? 'Passo 3 de 3: Produtividade' : 'Step 3 of 3: Productivity',
        title: isPt ? 'Paleta de Comandos (Ctrl+K) & Modos' : 'Command Palette (Ctrl+K) & Modes',
        desc: isPt ? 
          'Para usuários iniciantes ou administradores veteranos: pressione <code>Ctrl+K</code> em qualquer tela para buscar filas, rodar ações ou copiar comandos úteis como <code>lpstat -p -d</code> e <code>cupsenable</code>.' : 
          'Press <code>Ctrl+K</code> from any page to instantly search queues, toggle between Beginner and Advanced modes, or copy vital CLI diagnostics.',
        features: [
          isPt ? '✓ Atalho universal Ctrl+K ou ⌘K em qualquer tela' : '✓ Universal shortcut Ctrl+K or ⌘K on any page',
          isPt ? '✓ Alternância contínua entre Modo Iniciante e Avançado' : '✓ Seamless Beginner and Advanced mode toggle',
          isPt ? '✓ Navegação 100% via teclado com total acessibilidade' : '✓ Full keyboard navigation & accessibility'
        ]
      }
    ];

    let currentStep = 0;

    tourModal = document.createElement('div');
    tourModal.id = 'cups-tour-modal';
    tourModal.className = 'tour-modal-backdrop';
    tourModal.setAttribute('role', 'dialog');
    tourModal.setAttribute('aria-modal', 'true');
    tourModal.setAttribute('aria-labelledby', 'tour-step-heading');

    function renderStep() {
      const s = steps[currentStep];
      tourModal.innerHTML = `
        <div class="tour-modal-box">
          <div class="tour-modal-header">
            <h3 class="tour-modal-title">
              <span class="prompt-marker">&gt;_</span>
              <span>${isPt ? 'Guia Rápido do CUPS' : 'CUPS Quick Tour'}</span>
            </h3>
            <button type="button" class="cmd-palette-close" id="tour-close-btn" aria-label="Fechar">✕</button>
          </div>
          <div class="tour-step-body">
            <span class="tour-step-badge">${s.badge}</span>
            <h4 class="tour-step-heading" id="tour-step-heading">${s.title}</h4>
            <p class="tour-step-desc">${s.desc}</p>
            <ul class="tour-features-list">
              ${s.features.map(f => `<li>${f}</li>`).join('')}
            </ul>
          </div>
          <div class="tour-modal-footer">
            <div class="tour-dots">
              ${steps.map((_, idx) => `<span class="tour-dot ${idx === currentStep ? 'active' : ''}"></span>`).join('')}
            </div>
            <div class="tour-nav-buttons">
              ${currentStep > 0 ? `
                <button type="button" class="tour-btn" id="tour-btn-prev">${isPt ? 'Anterior' : 'Previous'}</button>
              ` : ''}
              ${currentStep < steps.length - 1 ? `
                <button type="button" class="tour-btn tour-btn-primary" id="tour-btn-next">${isPt ? 'Próximo →' : 'Next →'}</button>
              ` : `
                <button type="button" class="tour-btn tour-btn-primary" id="tour-btn-finish">${isPt ? 'Concluir ✓' : 'Finish ✓'}</button>
              `}
            </div>
          </div>
        </div>
      `;

      const closeBtn = tourModal.querySelector('#tour-close-btn');
      if (closeBtn) closeBtn.addEventListener('click', closeTourModal);

      const prevBtn = tourModal.querySelector('#tour-btn-prev');
      if (prevBtn) prevBtn.addEventListener('click', function() {
        if (currentStep > 0) {
          currentStep--;
          renderStep();
        }
      });

      const nextBtn = tourModal.querySelector('#tour-btn-next');
      if (nextBtn) nextBtn.addEventListener('click', function() {
        if (currentStep < steps.length - 1) {
          currentStep++;
          renderStep();
        }
      });

      const finishBtn = tourModal.querySelector('#tour-btn-finish');
      if (finishBtn) finishBtn.addEventListener('click', function() {
        closeTourModal();
        showToast(isPt ? 'Tour concluído! Bom proveito no CUPS.' : 'Tour complete! Enjoy CUPS.');
      });
    }

    renderStep();
    document.body.appendChild(tourModal);

    tourModal.addEventListener('click', function(e) {
      if (e.target === tourModal) closeTourModal();
    });
  }

  function closeTourModal() {
    const tourModal = document.getElementById('cups-tour-modal');
    if (tourModal) tourModal.remove();
  }

  /**
   * 9. Paleta de Comandos Raycast/Spotlight (Ctrl+K / ⌘K)
   */
  function initCommandPalette() {
    const paletteBtn = document.getElementById('btn-cmd-palette');
    if (paletteBtn) {
      paletteBtn.addEventListener('click', function(e) {
        e.preventDefault();
        openCommandPalette();
      });
    }
  }

  function toggleCommandPalette() {
    const existing = document.getElementById('cups-cmd-palette');
    if (existing) {
      closeCommandPalette();
    } else {
      openCommandPalette();
    }
  }

  function closeCommandPalette() {
    const palette = document.getElementById('cups-cmd-palette');
    if (palette) palette.remove();
  }

  function openCommandPalette() {
    const isPt = document.documentElement.lang ? document.documentElement.lang.startsWith('pt') : true;
    closeTourModal();
    closeHelpModal();
    closeSettingsModal();

    let palette = document.getElementById('cups-cmd-palette');
    if (palette) palette.remove();

    const items = [
      // Ações Principais
      {
        group: isPt ? 'Ações Rápidas' : 'Quick Actions',
        icon: '✦',
        title: isPt ? 'Adicionar Nova Impressora' : 'Add New Printer',
        sub: isPt ? 'Configurar uma nova fila de impressão local ou de rede' : 'Configure a local or network print queue',
        badge: '/admin',
        action: function() { window.location.href = '/admin/?OP=add-printer'; }
      },
      {
        group: isPt ? 'Ações Rápidas' : 'Quick Actions',
        icon: '🔍',
        title: isPt ? 'Encontrar Novas Impressoras' : 'Find New Printers',
        sub: isPt ? 'Descobrir dispositivos IPP e Bonjour/mDNS na rede' : 'Discover local IPP and mDNS broadcast printers',
        badge: '/admin',
        action: function() { window.location.href = '/admin/?OP=find-new-printers'; }
      },
      {
        group: isPt ? 'Ações Rápidas' : 'Quick Actions',
        icon: '🖨️',
        title: isPt ? 'Gerenciar Todas as Impressoras' : 'Manage All Printers',
        sub: isPt ? 'Exibir lista completa de filas e seus status' : 'View all print queues and current hardware states',
        badge: '/printers/',
        action: function() { window.location.href = '/printers/'; }
      },
      {
        group: isPt ? 'Ações Rápidas' : 'Quick Actions',
        icon: '📄',
        title: isPt ? 'Gerenciar Trabalhos de Impressão' : 'Manage Print Jobs',
        sub: isPt ? 'Visualizar fila de impressão ativa, pausar ou cancelar' : 'View spool queue, pause or cancel jobs',
        badge: '/jobs/',
        action: function() { window.location.href = '/jobs/'; }
      },
      {
        group: isPt ? 'Ações Rápidas' : 'Quick Actions',
        icon: '⚙️',
        title: isPt ? 'Configurações de Administração' : 'Administration Settings',
        sub: isPt ? 'Compartilhamento, logging de debug e permissões' : 'Server sharing, debug logging, and permissions',
        badge: '/admin',
        action: function() { window.location.href = '/admin/'; }
      },
      {
        group: isPt ? 'Ações Rápidas' : 'Quick Actions',
        icon: '🚀',
        title: isPt ? 'Iniciar Tour Rápido Interativo' : 'Start Interactive Quick Tour',
        sub: isPt ? 'Conheça os principais recursos do CUPS Root Intelligence' : 'Explore the key features of CUPS Root Intelligence',
        badge: 'Tour',
        action: function() { closeCommandPalette(); openTourModal(); }
      },
      {
        group: isPt ? 'Modo de Usuário' : 'User Mode',
        icon: '⚡',
        title: isPt ? 'Alternar Modo (Iniciante ⇄ Avançado)' : 'Toggle Mode (Beginner ⇄ Advanced)',
        sub: isPt ? 'Alterna exibição entre guias práticos e métricas brutas IPP' : 'Switch between friendly onboarding and raw IPP telemetry',
        badge: 'Mode',
        action: function() {
          closeCommandPalette();
          const currentMode = document.documentElement.classList.contains('mode-advanced') ? 'advanced' : 'beginner';
          applyUserMode(currentMode === 'beginner' ? 'advanced' : 'beginner', true);
        }
      },
      // Filas Frequentes
      {
        group: isPt ? 'Filas de Impressão' : 'Print Queues',
        icon: '🖨️',
        title: 'LaserJet-Pro-M404dn',
        sub: isPt ? 'HP LaserJet Pro M404dn • Bancada 03 (Ociosa)' : 'HP LaserJet Pro M404dn • Bench 03 (Idle)',
        badge: 'Queue',
        action: function() { window.location.href = '/printers/LaserJet-Pro-M404dn'; }
      },
      {
        group: isPt ? 'Filas de Impressão' : 'Print Queues',
        icon: '⚠️',
        title: 'Zebra-GK420t-Etiquetas',
        sub: isPt ? 'Almoxarifado • Alerta: Fim de fita / Ribbon out' : 'Warehouse • Alert: Ribbon out',
        badge: 'Queue',
        action: function() { window.location.href = '/printers/Zebra-GK420t-Etiquetas'; }
      },
      {
        group: isPt ? 'Filas de Impressão' : 'Print Queues',
        icon: '🖨️',
        title: 'WorkCentre-7835-Cor',
        sub: isPt ? 'Xerox Multifuncional • Processando' : 'Xerox Multifunctional • Processing',
        badge: 'Queue',
        action: function() { window.location.href = '/printers/WorkCentre-7835-Cor'; }
      },
      // Comandos de Terminal
      {
        group: isPt ? 'Comandos de Terminal (Copiar)' : 'Terminal Commands (Copy)',
        icon: '$',
        title: 'lpstat -p -d',
        sub: isPt ? 'Listar status de todas as filas e verificar qual é a padrão' : 'List status of all print queues and default destination',
        badge: 'CLI',
        action: function() {
          closeCommandPalette();
          copyTextToClipboard('lpstat -p -d', function() {
            showToast(isPt ? 'Comando "$ lpstat -p -d" copiado!' : 'Command "$ lpstat -p -d" copied!');
          });
        }
      },
      {
        group: isPt ? 'Comandos de Terminal (Copiar)' : 'Terminal Commands (Copy)',
        icon: '$',
        title: 'lpstat -o',
        sub: isPt ? 'Consultar trabalhos ativos no spooler local do CUPS' : 'Query active print jobs in local CUPS spooler',
        badge: 'CLI',
        action: function() {
          closeCommandPalette();
          copyTextToClipboard('lpstat -o', function() {
            showToast(isPt ? 'Comando "$ lpstat -o" copiado!' : 'Command "$ lpstat -o" copied!');
          });
        }
      },
      {
        group: isPt ? 'Comandos de Terminal (Copiar)' : 'Terminal Commands (Copy)',
        icon: '$',
        title: 'cupsenable <fila>',
        sub: isPt ? 'Reativar fila de impressão pausada ou travada' : 'Resume a stopped or paused print queue',
        badge: 'CLI',
        action: function() {
          closeCommandPalette();
          copyTextToClipboard('cupsenable LaserJet-Pro-M404dn', function() {
            showToast(isPt ? 'Comando "$ cupsenable" copiado!' : 'Command "$ cupsenable" copied!');
          });
        }
      },
      {
        group: isPt ? 'Comandos de Terminal (Copiar)' : 'Terminal Commands (Copy)',
        icon: '$',
        title: 'cancel -a',
        sub: isPt ? 'Cancelar todos os trabalhos pendentes em todas as filas' : 'Cancel all pending jobs across all queues',
        badge: 'CLI',
        action: function() {
          closeCommandPalette();
          copyTextToClipboard('cancel -a', function() {
            showToast(isPt ? 'Comando "$ cancel -a" copiado!' : 'Command "$ cancel -a" copied!');
          });
        }
      }
    ];

    palette = document.createElement('div');
    palette.id = 'cups-cmd-palette';
    palette.className = 'cmd-palette-backdrop';
    palette.innerHTML = `
      <div class="cmd-palette-box" role="dialog" aria-modal="true" aria-label="Command Palette">
        <div class="cmd-palette-input-wrap">
          <span class="cmd-palette-prompt">&gt;_</span>
          <input type="text" class="cmd-palette-input" id="cmd-palette-query" placeholder="${isPt ? 'Digite para buscar ações, filas ou comandos...' : 'Type to search actions, queues, or commands...'}" autocomplete="off" autofocus>
          <button type="button" class="cmd-palette-close" id="cmd-palette-close-btn" aria-label="Fechar">Esc</button>
        </div>
        <ul class="cmd-palette-list" id="cmd-palette-results"></ul>
        <div class="cmd-palette-footer">
          <div><kbd>↑</kbd><kbd>↓</kbd> ${isPt ? 'Navegar' : 'Navigate'} &bull; <kbd>↵</kbd> ${isPt ? 'Executar' : 'Select'} &bull; <kbd>Esc</kbd> ${isPt ? 'Fechar' : 'Close'}</div>
          <div><strong>CUPS Root Intelligence</strong></div>
        </div>
      </div>
    `;
    document.body.appendChild(palette);

    const input = palette.querySelector('#cmd-palette-query');
    const resultsList = palette.querySelector('#cmd-palette-results');
    const closeBtn = palette.querySelector('#cmd-palette-close-btn');

    let selectedIdx = 0;
    let filteredItems = items.slice();

    function renderList() {
      resultsList.innerHTML = '';
      if (!filteredItems.length) {
        resultsList.innerHTML = `<li class="cmd-palette-empty">${isPt ? 'Nenhuma ação ou fila encontrada.' : 'No matching actions or queues found.'}</li>`;
        return;
      }

      let currentGroup = '';

      filteredItems.forEach(function(item, idx) {
        if (item.group !== currentGroup) {
          currentGroup = item.group;
          const grp = document.createElement('li');
          grp.className = 'cmd-palette-group-title';
          grp.textContent = currentGroup;
          resultsList.appendChild(grp);
        }

        const li = document.createElement('li');
        li.className = 'cmd-palette-item' + (idx === selectedIdx ? ' selected' : '');
        li.setAttribute('data-idx', idx);
        li.innerHTML = `
          <span class="cmd-item-icon">${escapeHtml(item.icon)}</span>
          <div class="cmd-item-details">
            <div class="cmd-item-title">${escapeHtml(item.title)}</div>
            <div class="cmd-item-sub">${escapeHtml(item.sub)}</div>
          </div>
          <span class="cmd-item-badge">${escapeHtml(item.badge)}</span>
        `;

        li.addEventListener('click', function() {
          item.action();
        });

        li.addEventListener('mouseenter', function() {
          selectedIdx = idx;
          updateSelectedClass();
        });

        resultsList.appendChild(li);
      });

      ensureSelectedVisible();
    }

    function updateSelectedClass() {
      const allLis = resultsList.querySelectorAll('.cmd-palette-item');
      allLis.forEach(function(li) {
        const itemIdx = parseInt(li.getAttribute('data-idx'), 10);
        if (itemIdx === selectedIdx) {
          li.classList.add('selected');
        } else {
          li.classList.remove('selected');
        }
      });
    }

    function ensureSelectedVisible() {
      const selectedEl = resultsList.querySelector('.cmd-palette-item.selected');
      if (selectedEl) {
        selectedEl.scrollIntoView({ block: 'nearest' });
      }
    }

    input.addEventListener('input', function() {
      const q = input.value.trim().toLowerCase();
      if (!q) {
        filteredItems = items.slice();
      } else {
        filteredItems = items.filter(function(item) {
          return item.title.toLowerCase().includes(q) ||
                 item.sub.toLowerCase().includes(q) ||
                 item.group.toLowerCase().includes(q) ||
                 item.badge.toLowerCase().includes(q);
        });
      }
      selectedIdx = 0;
      renderList();
    });

    input.addEventListener('keydown', function(e) {
      if (e.key === 'ArrowDown') {
        e.preventDefault();
        if (filteredItems.length > 0) {
          selectedIdx = (selectedIdx + 1) % filteredItems.length;
          updateSelectedClass();
          ensureSelectedVisible();
        }
      } else if (e.key === 'ArrowUp') {
        e.preventDefault();
        if (filteredItems.length > 0) {
          selectedIdx = (selectedIdx - 1 + filteredItems.length) % filteredItems.length;
          updateSelectedClass();
          ensureSelectedVisible();
        }
      } else if (e.key === 'Enter') {
        e.preventDefault();
        if (filteredItems[selectedIdx]) {
          filteredItems[selectedIdx].action();
        }
      } else if (e.key === 'Escape') {
        e.preventDefault();
        closeCommandPalette();
      }
    });

    closeBtn.addEventListener('click', closeCommandPalette);
    palette.addEventListener('click', function(e) {
      if (e.target === palette) closeCommandPalette();
    });

    renderList();
    setTimeout(function() {
      input.focus();
    }, 20);
  }

  function escapeHtml(str) {
    if (!str) return '';
    return String(str)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#039;');
  }

})();
