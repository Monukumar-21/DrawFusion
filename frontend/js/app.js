// If running locally via python -m http.server 5500, point directly to the C++ server on 9001
const isLocalDev = window.location.port === '5500';
const API_URL = isLocalDev ? 'http://localhost:9001/api' : `${window.location.protocol}//${window.location.host}/api`;
const WS_URL = isLocalDev ? 'ws://localhost:9001/' : `${window.location.protocol === 'https:' ? 'wss:' : 'ws:'}//${window.location.host}/ws`;

// State
let token = sessionStorage.getItem('drawfusion_token');
let username = sessionStorage.getItem('drawfusion_username');
let ws = null;
let currentLobby = null;
let currentPrompt = "";
let timerInterval = null;

const els = {
    screens: document.querySelectorAll('.screen'),
    loading: document.getElementById('loading-overlay'),
    loadingText: document.getElementById('loading-text'),
    aiLoading: document.getElementById('ai-loading-overlay'),
    
    // Auth
    tabLogin: document.getElementById('tab-login'),
    tabRegister: document.getElementById('tab-register'),
    authForm: document.getElementById('auth-form'),
    authUsername: document.getElementById('username'),
    authPassword: document.getElementById('password'),
    authError: document.getElementById('auth-error'),
    
    // Menu
    welcomeUser: document.getElementById('welcome-user'),
    btnCreateLobby: document.getElementById('btn-create-lobby'),
    btnJoinLobby: document.getElementById('btn-join-lobby'),
    joinCode: document.getElementById('join-code'),
    btnDeleteAccount: document.getElementById('btn-delete-account'),
    btnChangePassword: document.getElementById('btn-change-password'),
    
    // API Keys
    hostGroqKey: document.getElementById('host-groq-key'),
    
    // Lobby
    lobbyIdDisplay: document.getElementById('lobby-id-display'),
    playerList: document.getElementById('player-list'),
    playerCount: document.getElementById('player-count'),
    btnStartGame: document.getElementById('btn-start-game'),
    btnReady: document.getElementById('btn-ready'),
    waitingMsg: document.getElementById('waiting-msg'),
    btnLeaveLobby: document.getElementById('btn-leave-lobby'),
    
    // Game
    currentPrompt: document.getElementById('current-prompt'),
    gameTimer: document.getElementById('game-timer'),
    btnSubmitDrawing: document.getElementById('btn-submit-drawing'),
    btnHint: document.getElementById('btn-hint'),
    btnClear: document.getElementById('btn-clear'),
    colorPicker: document.getElementById('color-picker'),
    brushSize: document.getElementById('brush-size'),
    hintDisplay: document.getElementById('hint-display'),
    btnQuitGame: document.getElementById('btn-quit-game'),
    
    // Results
    leaderboard: document.getElementById('leaderboard'),
    btnNextRound: document.getElementById('btn-next-round'),
    btnResultsMenu: document.getElementById('btn-results-menu'),
    
    // New UI Layout Elements
    gamePlayerList: document.getElementById('game-player-list'),
    gameEventLog: document.getElementById('game-event-log'),
    peekOverlay: document.getElementById('peek-overlay-container'),
    peekTargetName: document.getElementById('peeking-target-name'),
    peekImageView: document.getElementById('peek-image-view'),
    inkOverlay: document.getElementById('ink-overlay'),
    
    // History
    btnMatchHistory: document.getElementById('btn-match-history'),
    historyList: document.getElementById('history-list'),
    btnBackMenu: document.getElementById('btn-back-menu')
};

const showScreen = (screenId) => {
    els.screens.forEach(s => s.classList.remove('active'));
    document.getElementById(screenId).classList.add('active');
};

const showLoading = (text = 'Loading...') => {
    els.loadingText.textContent = text;
    els.loading.classList.add('active');
};

const showAILoading = () => {
    els.aiLoading.classList.add('active');
};

const hideLoading = () => {
    els.loading.classList.remove('active');
    if(els.aiLoading) els.aiLoading.classList.remove('active');
};

const logGameEvent = (msg, isBad = false) => {
    if (!els.gameEventLog) return;
    const li = document.createElement('li');
    li.textContent = msg;
    if (isBad) li.classList.add('bad-event');
    els.gameEventLog.appendChild(li);
    els.gameEventLog.scrollTop = els.gameEventLog.scrollHeight;
};

let isLoginMode = true;

els.tabLogin.addEventListener('click', () => {
    isLoginMode = true;
    els.tabLogin.classList.add('active');
    els.tabRegister.classList.remove('active');
    els.authError.textContent = '';
});

els.tabRegister.addEventListener('click', () => {
    isLoginMode = false;
    els.tabRegister.classList.add('active');
    els.tabLogin.classList.remove('active');
    els.authError.textContent = '';
});

els.authForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    const user = els.authUsername.value.trim();
    const pass = els.authPassword.value.trim();
    
    if (!user || !pass) return;
    
    showLoading('Authenticating...');
    els.authError.textContent = '';
    
    try {
        const endpoint = isLoginMode ? '/login' : '/register';
        const payload = { username: user, password: pass, email: user + '@test.com' };
        
        const res = await fetch(API_URL + endpoint, {
            method: 'POST',
            body: JSON.stringify(payload)
        });
        const data = await res.json();
        
        if (!res.ok) throw new Error(data.error || 'Auth failed');
        
        if (isLoginMode) {
            token = data.token;
            username = data.username;
            sessionStorage.setItem('drawfusion_token', token);
            sessionStorage.setItem('drawfusion_username', username);
            connectWebSocket();
        } else {
            // Auto login after register
            isLoginMode = true;
            els.tabLogin.click();
            els.authForm.dispatchEvent(new Event('submit'));
        }
    } catch (err) {
        els.authError.textContent = err.message;
        hideLoading();
    }
});

const connectWebSocket = () => {
    if (!token) return;
    
    showLoading('Connecting to server...');
    ws = new WebSocket(WS_URL);
    
    ws.onopen = () => {
        ws.send(JSON.stringify({ type: 'authenticate', token }));
    };
    
    ws.onmessage = (e) => {
        const msg = JSON.parse(e.data);
        console.log("WS Received:", msg);
        
        switch (msg.type) {
            case 'auth_success':
                hideLoading();
                username = msg.username;
                els.welcomeUser.textContent = username;
                showScreen('menu-screen');
                break;
                
            case 'security_alert':
                alert(msg.message);
                break;
                
            case 'password_changed':
                alert("Password changed successfully!");
                break;
                
            case 'lobby_created':
                currentLobby = msg.code;
                els.lobbyIdDisplay.textContent = currentLobby;
                updatePlayerList([username]); // host is in
                els.btnStartGame.style.display = 'inline-block';
                els.btnStartGame.disabled = true;
                els.waitingMsg.style.display = 'block';
                els.waitingMsg.textContent = "Waiting for players to be ready...";
                isReady = false;
                els.btnReady.textContent = 'Ready!';
                els.btnReady.classList.replace('btn-primary', 'btn-secondary');
                hideLoading();
                showScreen('lobby-screen');
                break;
                
            case 'lobby_joined':
                currentLobby = msg.code;
                els.lobbyIdDisplay.textContent = currentLobby;
                updatePlayerList(msg.players || [username]);
                els.btnStartGame.style.display = 'none';
                els.waitingMsg.style.display = 'block';
                els.waitingMsg.textContent = "Waiting for players to be ready...";
                isReady = false;
                els.btnReady.textContent = 'Ready!';
                els.btnReady.classList.replace('btn-primary', 'btn-secondary');
                hideLoading();
                showScreen('lobby-screen');
                break;
                
            case 'player_joined':
                addPlayerToList(msg.username);
                break;
                
            case 'player_left':
                removePlayerFromList(msg.username);
                break;
                
            case 'player_ready':
                updatePlayerReadyStatus(msg.username, msg.ready);
                break;
                
            case 'all_ready':
                els.btnStartGame.disabled = false;
                els.waitingMsg.textContent = "Everyone is ready!";
                break;
                
            case 'not_all_ready':
                els.btnStartGame.disabled = true;
                els.waitingMsg.textContent = "Waiting for players to be ready...";
                break;
                
            case 'join_attempt_full':
                alert(`Player ${msg.username} attempted to join, but the lobby is full.`);
                break;
                
            case 'kicked':
                alert("You have been kicked from the lobby by the host.");
                currentLobby = null;
                showScreen('menu-screen');
                break;
                
            case 'lobby_terminated':
                alert("The host has left the lobby. The lobby has been terminated.");
                currentLobby = null;
                showScreen('menu-screen');
                break;
                
            case 'left_lobby':
                currentLobby = null;
                showScreen('menu-screen');
                break;
                
            case 'peek_alert':
                logGameEvent(`${msg.peeker} is peeking at you!`, true);
                if (!window.peekInterval && canvasMgr) {
                    window.peekInterval = setInterval(() => {
                        ws.send(JSON.stringify({ type: 'peek_stream', image: canvasMgr.getBase64() }));
                    }, 250);
                }
                break;
                
            case 'stop_peek_alert':
                if (window.peekInterval) {
                    clearInterval(window.peekInterval);
                    window.peekInterval = null;
                }
                break;
                
            case 'peek_stream':
                els.peekImageView.src = msg.image;
                break;
                
            case 'sabotage_alert':
                logGameEvent(`${msg.attacker} used ${msg.attack} on you!`, true);
                if (msg.attack === 'ink') {
                    els.inkOverlay.style.display = 'block';
                    setTimeout(() => els.inkOverlay.style.display = 'none', 3000);
                }
                break;
                
            case 'game_event':
                logGameEvent(msg.message, msg.bad);
                break;
                
            case 'hint_response':
                els.hintDisplay.style.display = 'block';
                els.hintDisplay.textContent = "AI Hint: " + msg.hint;
                setTimeout(() => els.hintDisplay.style.display = 'none', 5000);
                break;
                
            case 'round_started':
                currentPrompt = msg.prompt;
                els.currentPrompt.textContent = currentPrompt;
                hideLoading();
                startGame(msg.end_time_ms);
                els.btnHint.disabled = false; // Re-enable for the new round
                break;
                
            case 'scoring_in_progress':
                showAILoading();
                break;
                
            case 'judging_results':
                hideLoading();
                showResults(msg.results, msg.free_trial);
                break;
                
            case 'history_results':
                hideLoading();
                showHistory(msg.matches);
                break;
                
            case 'error':
                alert('Server Error: ' + msg.message);
                hideLoading();
                break;
                
            case 'account_deleted':
                sessionStorage.removeItem('drawfusion_token');
                sessionStorage.removeItem('drawfusion_username');
                token = null;
                username = null;
                hideLoading();
                alert("Your account has been permanently deleted.");
                showScreen('auth-screen');
                break;
        }
    };
    
    ws.onclose = () => {
        hideLoading();
        alert("Disconnected from server");
        showScreen('auth-screen');
    };
};

els.btnCreateLobby.addEventListener('click', () => {
    const groq = els.hostGroqKey ? els.hostGroqKey.value.trim() : '';
    showLoading(groq ? 'Validating Key & Creating lobby...' : 'Starting Free Trial Lobby...');
    ws.send(JSON.stringify({ type: 'create_lobby', groq_key: groq }));
});

els.btnJoinLobby.addEventListener('click', () => {
    const code = els.joinCode.value.trim().toUpperCase();
    if (!code) return;
    showLoading('Joining lobby...');
    ws.send(JSON.stringify({ type: 'join_lobby', code }));
});

els.btnStartGame.addEventListener('click', () => {
    if (els.btnStartGame.disabled) return;
    showLoading('Generating Prompt...');
    ws.send(JSON.stringify({ type: 'get_prompt', difficulty: 'medium' }));
});

let isReady = false;
els.btnReady.addEventListener('click', () => {
    isReady = !isReady;
    els.btnReady.textContent = isReady ? 'Not Ready' : 'Ready!';
    if (isReady) {
        els.btnReady.classList.replace('btn-secondary', 'btn-primary');
    } else {
        els.btnReady.classList.replace('btn-primary', 'btn-secondary');
    }
    ws.send(JSON.stringify({ type: 'set_ready', ready: isReady }));
});

els.btnMatchHistory.addEventListener('click', () => {
    showLoading('Loading History...');
    ws.send(JSON.stringify({ type: 'get_history' }));
});

els.btnBackMenu.addEventListener('click', () => {
    showScreen('menu-screen');
});

els.btnDeleteAccount.addEventListener('click', () => {
    if (confirm('Are you sure you want to delete your account? This cannot be undone.')) {
        ws.send(JSON.stringify({ type: 'delete_account' }));
    }
});

els.btnChangePassword.addEventListener('click', () => {
    const newPassword = prompt("Enter your new password:");
    if (newPassword && newPassword.trim().length > 0) {
        ws.send(JSON.stringify({ type: 'change_password', new_password: newPassword.trim() }));
    }
});

els.btnLeaveLobby.addEventListener('click', () => {
    if (confirm("Are you sure you want to leave the lobby?")) {
        ws.send(JSON.stringify({ type: 'leave_lobby' }));
    }
});
els.btnQuitGame.addEventListener('click', () => {
    clearInterval(timerInterval);
    showScreen('menu-screen');
});
els.btnResultsMenu.addEventListener('click', () => {
    if (confirm("Are you sure you want to leave the lobby?")) {
        ws.send(JSON.stringify({ type: 'leave_lobby' }));
    }
});

let players = [];
const updatePlayerList = (list) => {
    players = list;
    els.playerList.innerHTML = '';
    players.forEach(p => addPlayerToList(p, false));
    els.playerCount.textContent = players.length;
};

const addPlayerToList = (p, checkExist = true) => {
    if (checkExist && players.includes(p)) return;
    if (checkExist) players.push(p);
    
    const isHost = (p === players[0]);
    const amIHost = (username === players[0]);
    
    const li = document.createElement('li');
    li.dataset.username = p;
    
    let html = `<strong>${p}</strong> ${isHost ? '<span class="host-badge">Host</span>' : ''}`;
    if (amIHost && !isHost) {
        html += `<button onclick="kickPlayer('${p}')" style="background:none; border:none; cursor:pointer; margin-left:10px; font-size:1.2rem;">❌</button>`;
    }
    
    li.innerHTML = html;
    els.playerList.appendChild(li);
    els.playerCount.textContent = players.length;
};

window.kickPlayer = (targetUsername) => {
    if (confirm(`Are you sure you want to kick ${targetUsername}?`)) {
        ws.send(JSON.stringify({ type: 'kick_player', username: targetUsername }));
    }
};

const removePlayerFromList = (p) => {
    players = players.filter(name => name !== p);
    updatePlayerList(players);
};

const updatePlayerReadyStatus = (user, ready) => {
    const listItems = Array.from(els.playerList.children);
    const li = listItems.find(item => item.dataset.username === user);
    if (li) {
        if (ready) {
            if (!li.innerHTML.includes('✔️')) li.innerHTML += ' <span class="ready-check">✔️</span>';
        } else {
            li.innerHTML = li.innerHTML.replace(' <span class="ready-check">✔️</span>', '');
        }
    }
};

let canvasMgr = null;
let isPeeking = false;
let peekTarget = "";
let peekPenaltyMs = 0;

const startGame = (end_time_ms) => {
    showScreen('game-screen');
    if (!canvasMgr) {
        canvasMgr = new window.CanvasManager('drawing-canvas');
        
        els.colorPicker.addEventListener('input', (e) => canvasMgr.setColor(e.target.value));
        els.brushSize.addEventListener('input', (e) => canvasMgr.setLineWidth(e.target.value));
        els.btnClear.addEventListener('click', () => canvasMgr.clear());
    }
    
    canvasMgr.resize();
    canvasMgr.clear();
    
    // Clear logs and flags
    window.hasUsedSabotage = false;
    els.gameEventLog.innerHTML = '';
    logGameEvent('Round started!');
    
    // Populate Game Player List with Action Buttons
    els.gamePlayerList.innerHTML = '';
    players.forEach(p => {
        const li = document.createElement('li');
        li.innerHTML = `<strong>${p}</strong>`;
        
        if (p !== username) { // Don't show buttons for self
            const actions = document.createElement('div');
            actions.className = 'player-actions';
            
            // Peek Button (Hold to peek)
            const btnPeek = document.createElement('button');
            btnPeek.className = 'btn-icon';
            btnPeek.textContent = '👀 Peek';
            
            const startPeeking = () => {
                if (isPeeking) return;
                isPeeking = true;
                peekTarget = p;
                els.peekTargetName.textContent = p;
                els.peekOverlay.style.display = 'flex';
                els.gameTimer.parentElement.classList.add('timer-penalty');
                ws.send(JSON.stringify({ type: 'start_peek', target: p }));
            };
            
            const stopPeeking = () => {
                if (!isPeeking) return;
                isPeeking = false;
                els.peekOverlay.style.display = 'none';
                els.peekImageView.src = '';
                els.gameTimer.parentElement.classList.remove('timer-penalty');
                ws.send(JSON.stringify({ type: 'stop_peek', target: p }));
            };
            
            btnPeek.addEventListener('mousedown', startPeeking);
            btnPeek.addEventListener('mouseup', stopPeeking);
            btnPeek.addEventListener('mouseleave', stopPeeking);
            
            // Touch support for mobile peek hold
            btnPeek.addEventListener('touchstart', (e) => { e.preventDefault(); startPeeking(); });
            btnPeek.addEventListener('touchend', stopPeeking);
            
            // Sabotage Button
            const btnSabotage = document.createElement('button');
            btnSabotage.className = 'btn-icon';
            btnSabotage.textContent = '🦑 Ink (-5s)';
            btnSabotage.onclick = () => {
                if (window.hasUsedSabotage) {
                    alert('You can only use Sabotage once per round!');
                    return;
                }
                if (confirm(`Spend 5 seconds to throw Ink at ${p}?`)) {
                    window.hasUsedSabotage = true;
                    btnSabotage.disabled = true;
                    peekPenaltyMs += 5000;
                    ws.send(JSON.stringify({ type: 'sabotage', target: p, attack: 'ink' }));
                }
            };
            
            actions.appendChild(btnPeek);
            actions.appendChild(btnSabotage);
            li.appendChild(actions);
        }
        
        els.gamePlayerList.appendChild(li);
    });
    
    // Sync Timer with Penalty System
    clearInterval(timerInterval);
    let lastTick = Date.now();
    peekPenaltyMs = 0;
    isPeeking = false;
    
    timerInterval = setInterval(() => {
        const now = Date.now();
        const delta = now - lastTick;
        lastTick = now;
        
        if (isPeeking) {
            peekPenaltyMs += delta; // 1x extra penalty = 2x total decay
        }
        
        const timeLeftMs = end_time_ms - now - peekPenaltyMs;
        
        if (timeLeftMs <= 0) {
            clearInterval(timerInterval);
            els.gameTimer.textContent = '0';
            els.gameTimer.parentElement.classList.remove('timer-penalty');
            if(isPeeking) {
                isPeeking = false;
                els.peekOverlay.style.display = 'none';
                ws.send(JSON.stringify({ type: 'stop_peek', target: peekTarget }));
            }
            els.btnSubmitDrawing.click();
        } else {
            els.gameTimer.textContent = Math.ceil(timeLeftMs / 1000);
        }
    }, 100);
};

els.btnSubmitDrawing.addEventListener('click', () => {
    clearInterval(timerInterval);
    
    // Downscale canvas to max 800x600 before sending
    const originalCanvas = document.getElementById('drawing-canvas');
    let width = originalCanvas.width;
    let height = originalCanvas.height;
    
    const MAX_WIDTH = 800;
    const MAX_HEIGHT = 600;
    
    if (width > MAX_WIDTH || height > MAX_HEIGHT) {
        const ratio = Math.min(MAX_WIDTH / width, MAX_HEIGHT / height);
        width = width * ratio;
        height = height * ratio;
    }
    
    const tempCanvas = document.createElement('canvas');
    tempCanvas.width = width;
    tempCanvas.height = height;
    const tempCtx = tempCanvas.getContext('2d');
    
    // Fill white background (since transparent pngs might ruin CNN inference)
    tempCtx.fillStyle = '#FFFFFF';
    tempCtx.fillRect(0, 0, width, height);
    tempCtx.drawImage(originalCanvas, 0, 0, width, height);
    
    const base64Img = tempCanvas.toDataURL('image/png');
    
    ws.send(JSON.stringify({
        type: 'submit_drawing',
        image: base64Img,
        prompt: currentPrompt,
        round_id: 'round-1'
    }));
    showAILoading();
});

els.btnHint.addEventListener('click', () => {
    if (els.btnHint.disabled) return;
    ws.send(JSON.stringify({ type: 'get_hint' }));
    els.btnHint.disabled = true; // Disable after one click
});

const showResults = (results, isFreeTrial = false) => {
    showScreen('results-screen');
    els.leaderboard.innerHTML = '';
    
    if (isFreeTrial) {
        els.btnNextRound.style.display = 'none';
    } else {
        els.btnNextRound.style.display = 'inline-block';
    }
    
    results.sort((a,b) => a.rank - b.rank).forEach(r => {
        const card = document.createElement('div');
        card.className = 'result-card';
        card.innerHTML = `
            <div class="result-rank">#${r.rank}</div>
            <div class="result-info">
                <h3>${r.player_id}</h3>
                <p>${r.feedback}</p>
            </div>
            <div class="result-score">${Math.round(r.score * 100)}%</div>
        `;
        els.leaderboard.appendChild(card);
    });
};

els.btnNextRound.addEventListener('click', () => {
    isReady = false;
    els.btnReady.textContent = 'Ready!';
    els.btnReady.classList.replace('btn-primary', 'btn-secondary');
    
    const listItems = Array.from(els.playerList.children);
    listItems.forEach(li => {
        li.innerHTML = li.innerHTML.replace(' <span class="ready-check">✔️</span>', '');
    });
    
    els.btnStartGame.disabled = true;
    els.waitingMsg.textContent = "Waiting for players to be ready...";
    
    showScreen('lobby-screen');
});

const showHistory = (matches) => {
    showScreen('history-screen');
    els.historyList.innerHTML = '';
    
    if (matches.length === 0) {
        els.historyList.innerHTML = '<p style="text-align:center; color: var(--text-secondary);">No match history found.</p>';
        return;
    }
    
    matches.forEach(m => {
        const card = document.createElement('div');
        card.className = 'history-card';
        
        let scoreDisplay = m.score > 0 ? `${Math.round(m.score * 100)}%` : 'Pending';
        
        card.innerHTML = `
            <img src="${m.image || ''}" alt="Drawing">
            <div class="history-info">
                <h3>Prompt: ${m.prompt}</h3>
                <p><strong>Feedback:</strong> ${m.feedback || 'None'}</p>
                <p style="font-size: 0.8rem; margin-top: 5px;">${new Date(m.date).toLocaleString()}</p>
            </div>
            <div class="history-score">${scoreDisplay}</div>
        `;
        els.historyList.appendChild(card);
    });
};

// Auto login attempt on load
if (token) {
    showLoading('Reconnecting...');
    connectWebSocket();
}
