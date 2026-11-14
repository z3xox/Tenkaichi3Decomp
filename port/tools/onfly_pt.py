#!/usr/bin/env python3
"""The fork's Portuguese for the online menus and the port's text: port/translations/port/pt.json.

    python3 port/tools/onfly_pt.py

Brings pt.json up to date with the menus' lines (tbo_strings.py does that part) and then writes the Portuguese below
over each line it has, found by its English. The wording is the fork's own, in the words of a travel and expense
company: a lobby is a "sala de embarque", its code a "localizador", its owner the "gestor", joining is the
"check-in", a tournament the "rinha" and its bracket the "itinerario", a winner is "aprovado". A line that is not
below stays in English (the fighters' names are left so on purpose). A line has to keep its %d / %s / {name} parts in
their order: the tool stops when one does not.
"""
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "port/translations/port/pt.json"

PT = {
    "Close the game?\nUnsaved progress will be lost.": "Fechar o jogo?\nO progresso não salvo será perdido.",
    "Connecting to your rival...": "Conectando ao seu rival...",
    'Your rival "{name}" is here! The match begins.': 'Seu rival "{name}" embarcou! A luta vai começar.',
    "You are in the lobby. Its screen opens from the main menu (Tenkaichi Budokai Online).":
        "Check-in feito na sala de embarque. A tela dela abre pelo menu principal (Rinha Online do Approval).",
    'Your rival "{name}" from {region} is here! The match begins.': 'Seu rival "{name}", vindo de {region}, embarcou! A luta vai começar.',
    "Round trip: ~{ms} ms": "Ida e volta: ~{ms} ms",
    "North America": "América do Norte", "South America": "América do Sul", "Europe": "Europa",
    "the Middle East": "Oriente Médio", "Africa": "África", "Asia": "Ásia", "Oceania": "Oceania",
    "Update {version} available. F1 to update.": "Atualização {version} disponível. F1 para atualizar.",
    "Downloading update {version}: {percent}%": "Baixando a atualização {version}: {percent}%",
    "Update {version} is ready. F1 > Restart now.": "Atualização {version} pronta. F1 > Reiniciar agora.",
    "Updated to {version}.": "Atualizado para {version}.",
    "An update is being installed: online play is off until the game restarts (F1 > Restart now).":
        "Uma atualização está sendo instalada: o online fica desligado até o jogo reiniciar (F1 > Reiniciar agora).",
    # the main list
    "Quick Match": "Reserva Expressa", "Ranked Match": "Luta Ranqueada", "Lobby": "Sala de Embarque", "Join": "Check-in",
    "LAN Match": "Voo Doméstico", "Friends": "Colegas", "Tournament": "Rinha", "Replays": "Comprovantes",
    "Leaderboard": "Ranking", "Profile": "Cartão de Embarque",
    "Fight whoever is searching right now! No time limit, and you can train while you wait.":
        "Lute com quem estiver procurando agora! Sem limite de tempo, e dá para treinar enquanto espera.",
    "Fights that move your rank, season by season. That one is coming soon!":
        "Lutas que valem posição no ranking, a cada fechamento. Essa está em aprovação!",
    "A lobby for your friends: up to eight, your rules, rematches, spectators and tournaments!":
        "Uma sala de embarque para a squad: até oito, sua política, revanches, lista de espera e rinhas!",
    "Got a lobby's code? Type it in to join, to play or to watch! By Address joins by the owner's address.":
        "Tem o localizador da sala? Digite e faça o check-in, para lutar ou assistir! Por Endereço usa o endereço do gestor.",
    "Friends on the same network? No internet needed: one hosts, the others type the address shown!":
        "Colegas na mesma rede? Não precisa de internet: um abre a sala, os outros digitam o endereço mostrado!",
    "Your rivals, who is online, and invites. That one is coming soon!":
        "Seus rivais, quem está online e convites. Essa está em aprovação!",
    "The Tenkaichi Budokai itself: brackets of 4 to 16 players. That one is coming soon!":
        "A Rinha em pessoa: itinerários de 4 a 16 viajantes. Essa está em aprovação!",
    "Your last matches, saved to watch again. That one is coming soon!":
        "Suas últimas lutas, guardadas como comprovante. Essa está em aprovação!",
    "The strongest in the world, among your friends, and with each fighter. Coming soon!":
        "Os mais fortes do mundo, da sua squad e com cada lutador. Em aprovação!",
    "The name your rivals see.": "O nome que seus rivais veem.",
    "AUTO": "AUTO", "NORTH AMERICA": "AMÉRICA DO NORTE", "SOUTH AMERICA": "AMÉRICA DO SUL", "EUROPE": "EUROPA",
    "MIDDLE EAST": "ORIENTE MÉDIO", "AFRICA": "ÁFRICA", "ASIA": "ÁSIA", "OCEANIA": "OCEANIA", "ANYWHERE": "QUALQUER LUGAR",
    "60 SECONDS": "60 SEGUNDOS", "90 SECONDS": "90 SEGUNDOS", "180 SECONDS": "180 SEGUNDOS", "240 SECONDS": "240 SEGUNDOS",
    "NO LIMIT": "SEM LIMITE", "AUTOMATIC": "AUTOMÁTICO", "%d FRAME": "%d QUADRO", "%d FRAMES": "%d QUADROS",
    "CHECKING YOUR GAME DATA AGAINST THE DISC... TRY AGAIN IN A MOMENT": "CONFERINDO SEUS DADOS COM O DISCO... TENTE DE NOVO EM INSTANTES",
    "Back": "Voltar",
    "THE NETWORK COULD NOT BE OPENED ON THIS PC": "NÃO FOI POSSÍVEL ABRIR A REDE NESTE PC",
    "THE PORT AFTER THE COLON IS A NUMBER FROM 1 TO 65535": "A PORTA DEPOIS DOS DOIS-PONTOS É UM NÚMERO DE 1 A 65535",
    "TYPE THE OWNER'S ADDRESS FIRST, AS 192.168.1.20 OR 192.168.1.20:7000": "DIGITE O ENDEREÇO DO GESTOR, COMO 192.168.1.20 OU 192.168.1.20:7000",
    "THE CONNECTION WAS REFUSED": "A CONEXÃO FOI REPROVADA",
    "THE TWO GAMES COULD NOT REACH EACH OTHER, EVEN THROUGH THE RELAY": "OS DOIS JOGOS NÃO SE ALCANÇARAM, NEM COM ESCALA",
    "THE OTHER GAME IS ANOTHER VERSION: BOTH NEED THE SAME RELEASE": "O OUTRO JOGO É DE OUTRA VERSÃO: OS DOIS PRECISAM DA MESMA",
    "THE SEARCH COULD NOT BE STARTED": "NÃO FOI POSSÍVEL INICIAR A BUSCA",
    "Train While Waiting": "Treinar na Espera", "Cancel": "Cancelar", "Change": "Alterar", "Start Training": "Começar o Treino",
    "Next": "Avançar", "Search": "Buscar",
    "Update available. Online servers are unreachable until you update.": "Atualização disponível. O online fica fora do ar até você atualizar.",
    "Enter": "Entrar",
    "NOT OPEN YET: IT NEEDS MORE FROM THE MATCHMAKING SERVICE": "AINDA EM APROVAÇÃO: FALTA A CENTRAL DE RESERVAS",
    "Open Lobby": "Abrir Sala", "Host by Address": "Abrir por Endereço",
    "DELETE": "APAGAR", "PASTE": "COLAR", "JOIN": "CHECK-IN", "BY ADDRESS": "POR ENDEREÇO", "SPACE": "ESPAÇO", "DONE": "PRONTO",
    "A LOBBY'S CODE HAS SIX LETTERS AND NUMBERS": "O LOCALIZADOR TEM SEIS LETRAS E NÚMEROS",
    "Host": "Abrir", "Delete": "Apagar", "Type Name": "Digitar Nome",
    "SAVED: YOUR RIVALS WILL SEE THIS NAME": "SALVO: SEUS RIVAIS VERÃO ESTE NOME",
    "SOON": "EM BREVE", "PORT %d": "PORTA %d", "WITH A CODE": "COM LOCALIZADOR", "A NEW LOBBY": "NOVA SALA",
    "Host a LAN Match": "Abrir um Voo Doméstico", "Join a LAN Match": "Check-in em Voo Doméstico",
    "ON YOUR NETWORK": "NA SUA REDE", "THE OWNER'S ADDRESS:PORT": "ENDEREÇO:PORTA DO GESTOR",
    "NO :PORT TYPED: PORT 7000": "SEM :PORTA DIGITADA: PORTA 7000",
    "QUICK MATCH": "RESERVA EXPRESSA", "TEAMS": "EQUIPES", "ONE ON ONE": "UM CONTRA UM",
    "YOUR RIVAL IS FROM": "SEU RIVAL VEM DE", "NO ONE THERE:": "NINGUÉM POR LÁ:", "SEARCHING EVERYWHERE": "BUSCANDO EM TODO LUGAR",
    "PLAYERS IN": "VIAJANTES EM", "FIRST, EVERYONE AFTER %d S": "PRIMEIRO, TODOS APÓS %d S", "NO ONE NEARBY:": "NINGUÉM POR PERTO:",
    "LOOKING FOR A RIVAL": "PROCURANDO UM RIVAL", "NEAR YOU FIRST": "PERTO DE VOCÊ PRIMEIRO",
    "THE SERVICE PAIRS YOU": "A CENTRAL TE COLOCA", "WITH THE NEXT RIVAL": "COM O PRÓXIMO RIVAL",
    "%d IN THIS QUEUE": "%d NESTA FILA", "JOINING LOBBY": "CHECK-IN NA SALA", "LOOKING FOR": "PROCURANDO", "THE LOBBY...": "A SALA...",
    "YOUR RIVAL IS HERE!": "SEU RIVAL EMBARCOU!", "SEARCHING...  %d:%02d": "BUSCANDO...  %d:%02d",
    "No Quick Match": "Sem Reserva Expressa",
    "Quick match needs the original disc's data.": "A reserva expressa exige os dados originais do disco.",
    "In a lobby with friends, both need the same data.": "Numa sala com colegas, os dois precisam dos mesmos dados.",
    "Changed: ": "Alterado: ",
    "Your game data is not the disc's.": "Seus dados de jogo não são os do disco.",
    "Your game data could not be checked.": "Não foi possível conferir seus dados de jogo.",
    "Take out what you added in gamedata/mods.": "Tire o que você colocou em gamedata/mods.",
    "(Fighter mods switch off by themselves online.)": "(Mods de lutador se desligam sozinhos no online.)",
    "BACK": "VOLTAR", "TYPE YOUR NAME": "DIGITE SEU NOME", "SPARRING AT THE TOURNAMENT": "AQUECIMENTO NA RINHA",
    "You": "Você", "Partner": "Parceiro", "QUICK MATCH SEARCH": "BUSCA DE RESERVA EXPRESSA", "JOINING A LOBBY": "CHECK-IN EM UMA SALA",
    "SEARCHING  %d:%02d": "BUSCANDO  %d:%02d", "THE MATCH STARTS THE MOMENT": "A LUTA COMEÇA ASSIM QUE",
    "YOUR RIVAL ARRIVES": "SEU RIVAL EMBARCAR", "PICK YOUR BATTLE": "ESCOLHA SUA LUTA", "Battle": "Luta",
    "TEAMS (UP TO 5)": "EQUIPES (ATÉ 5)", "Region": "Região", "WAITING RIGHT NOW": "ESPERANDO AGORA",
    "Teams": "Equipes", "One on One": "Um contra Um", "%d WAITING": "%d ESPERANDO",
    "PLAYERS THERE FIRST, EVERYONE AFTER %d S": "QUEM ESTÁ LÁ PRIMEIRO, TODOS APÓS %d S",
    "RIVALS NEAR YOU FIRST, THEN ANYWHERE": "RIVAIS PERTO PRIMEIRO, DEPOIS QUALQUER LUGAR",
    "THE NEXT RIVAL, WHEREVER: THE FASTEST": "O PRÓXIMO RIVAL, DE ONDE FOR: O MAIS RÁPIDO",
    "Name": "Nome", "PLAYER CARD": "CARTÃO DE EMBARQUE",
    "SINGLE BATTLE": "LUTA INDIVIDUAL", "TEAM BATTLE": "LUTA EM EQUIPE",
    "DP BATTLE: 10 DP": "LUTA DP: 10 DP", "DP BATTLE: 15 DP": "LUTA DP: 15 DP", "DP BATTLE: 20 DP": "LUTA DP: 20 DP",
    "Mode": "Modo", "Time Limit": "Tempo", "Game Speed": "Velocidade", "Rollback": "Rollback", "Input Delay": "Atraso",
    "Entrants": "Viajantes",
    # the lobby's reasons and notes
    "The owner went back to the lobby.": "O gestor voltou para a sala de embarque.",
    "The two players could not connect to each other.": "Os dois viajantes não conseguiram se conectar.",
    "The lobby was closed by its owner.": "A sala foi fechada pelo gestor.",
    "The lobby's owner removed you from the lobby.": "O gestor cancelou sua reserva na sala.",
    "The connection to the lobby's owner was lost: the lobby is closed.": "A conexão com o gestor caiu: a sala foi fechada.",
    "This lobby is full (8 places).": "Esta sala está lotada (8 assentos).",
    "The lobby owner's game is a different version: joining needs the same release.":
        "O jogo do gestor é de outra versão: o check-in exige a mesma versão.",
    "The game at that address is a different version: joining needs the same release.":
        "O jogo nesse endereço é de outra versão: o check-in exige a mesma versão.",
    "Someone with another version of the game tried to join.": "Alguém com outra versão do jogo tentou fazer check-in.",
    "The lobby owner's game data are not the same as yours (a modified disc image or mods on one side): you cannot join.":
        "Os dados de jogo do gestor não são iguais aos seus (disco modificado ou mods de um lado): check-in reprovado.",
    "That address is not known.": "Esse endereço não existe.",
    "No socket could be opened.": "Não foi possível abrir a conexão.",
    "Lobbies need the game's state list (the .mem file beside the program): copy all the files of the release.":
        "As salas precisam da lista de estado do jogo (o arquivo .mem ao lado do programa): copie todos os arquivos da versão.",
    "The tournament was called off.": "A rinha foi cancelada.",
    "The match will be played again.": "A luta será refeita.",
    "No winner: the match will be played again.": "Ninguém aprovado: a luta será refeita.",
    "The players' results differ: the match will be played again.": "As prestações de contas não batem: a luta será refeita.",
    "Only one player's result came: the match will be played again.": "Só chegou a prestação de contas de um: a luta será refeita.",
    "The two games disagree on this match: the owner chooses the winner.": "As contas desta luta não batem: o gestor escolhe o aprovado.",
    "The host's game data are not the same as yours (a modified disc image or mods on one side): you cannot watch this match.":
        "Os dados de jogo do anfitrião não são iguais aos seus (disco modificado ou mods de um lado): você não pode assistir.",
    "This match already has as many spectators as it can take (6).": "A lista de espera desta luta está lotada (6).",
    "The host does not let anyone watch its matches.": "O anfitrião não deixa ninguém assistir às lutas dele.",
    "This match cannot be watched (it was not started from the online menus).": "Esta luta não pode ser assistida (não começou pelos menus online).",
    "That is the address of the player who joined, not of the host: spectators connect to the host.":
        "Esse é o endereço de quem fez check-in, não do anfitrião: quem assiste conecta no anfitrião.",
    "This match is a lobby's: join the lobby (its code, or its owner's address) to watch it.":
        "Esta luta é de uma sala: faça check-in na sala (localizador ou endereço do gestor) para assistir.",
    "The host's game is a different version: watching needs the same release as the host's.":
        "O jogo do anfitrião é de outra versão: assistir exige a mesma versão dele.",
    "No answer from the host. Check the address and port. Watching a match needs a release newer than 0.1.38.7 on the host as well (an older host does not answer spectators).":
        "O anfitrião não respondeu. Confira o endereço e a porta. Assistir exige uma versão mais nova que a 0.1.38.7 também no anfitrião (um mais antigo não responde a quem assiste).",
    "Your game data and the other player's are not the same (a modified disc image or mods on one side): an online match needs the same on both.":
        "Seus dados de jogo e os do outro viajante não são iguais (disco modificado ou mods de um lado): a luta online exige os mesmos nos dois.",
    "Too many have asked to watch that room in the last two minutes; try again soon.":
        "Gente demais pediu para assistir a essa sala nos últimos dois minutos; tente de novo em instantes.",
    "No room with that code, or the matchmaking service does not take spectators yet. (Codes last ten minutes; a host on 0.1.38.7 or older gives its room up when the match begins.)":
        "Nenhuma sala com esse localizador, ou a central de reservas ainda não aceita lista de espera. (O localizador vale dez minutos; um anfitrião na 0.1.38.7 ou anterior libera a sala quando a luta começa.)",
    "The matchmaking service could not be reached (is the network up?).": "A central de reservas não respondeu (a rede está no ar?).",
    "Too many wrong codes from this address; try again in an hour.": "Localizadores errados demais deste endereço; tente de novo em uma hora.",
    "%s joined the lobby": "%s fez check-in na sala", "%s left the lobby": "%s saiu da sala",
    "%s was removed from the lobby": "%s teve a reserva cancelada", "%s lost the connection to the lobby": "%s perdeu a conexão com a sala",
    "%s won the match": "%s foi aprovado na luta", "%s won by walkover": "%s foi aprovado por no-show",
    "%s is the champion!": "%s é o Aprovador-Geral!",
    "%s lost the connection: the place is held for 90 s": "%s perdeu a conexão: o assento fica reservado por 90 s",
    "%s is back in the tournament": "%s voltou para a rinha", "%s did not come back in time": "%s não voltou a tempo: no-show",
    "%s won the match (the owner's choice)": "%s foi aprovado na luta (decisão do gestor)",
    "Tournament: %d entrants": "Rinha: %d viajantes", "Match %d: %s vs %s": "Luta %d: %s vs %s",
    "TOURNAMENT": "RINHA", "VERSUS": "VERSUS", "%d PLAYERS": "%d VIAJANTES", "%d%% (STANDARD)": "%d%% (PADRÃO)",
    "OFF": "DESLIGADO", "UP TO %d FRAMES": "ATÉ %d QUADROS",
    "Match On": "Luta em Curso", "Start Next Match": "Próxima Decolagem", "New Tournament": "Nova Rinha",
    "Start Tournament": "Começar a Rinha", "Start Match": "Decolar", "Bracket": "Itinerário",
    "Not Ready": "Sem Check-in", "Ready": "Check-in Feito", "Chat": "Chat", "Close Lobby": "Fechar Sala", "Leave Lobby": "Sair da Sala",
    "A MATCH IS ON ALREADY": "JÁ HÁ UMA LUTA EM CURSO",
    "TWO PLAYERS ARE NEEDED: ONE IN 1P, ONE IN 2P": "FALTAM DOIS VIAJANTES: UM NO 1P, UM NO 2P",
    "CHOOSE THE WINNER OF THE DISPUTED MATCH ON THE BRACKET": "ESCOLHA NO ITINERÁRIO O APROVADO DA LUTA EM AUDITORIA",
    "WAITING FOR A PLAYER WHO LOST THE CONNECTION": "ESPERANDO UM VIAJANTE QUE PERDEU A CONEXÃO",
    "NO MATCH LEFT TO PLAY": "NÃO HÁ MAIS LUTAS",
    "PUT AT LEAST TWO PLAYERS IN THE PLAYER PLACES": "COLOQUE PELO MENOS DOIS VIAJANTES NOS ASSENTOS",
    "Send": "Enviar", "Yes": "Sim", "No": "Não", "Move": "Remarcar", "Remove from the Lobby": "Cancelar a Reserva", "OK": "OK",
    "Move Here": "Remarcar Aqui", "Copy Code": "Copiar Localizador", "Copy Address": "Copiar Endereço",
    "THE ENTRANTS CANNOT CHANGE DURING A TOURNAMENT": "OS VIAJANTES NÃO MUDAM DURANTE UMA RINHA",
    "THE RULES CANNOT CHANGE DURING A MATCH": "A POLÍTICA NÃO MUDA DURANTE UMA LUTA",
    "NOT DURING A MATCH: THE PLACES CHANGE BETWEEN MATCHES": "DURANTE A LUTA NÃO: OS ASSENTOS MUDAM ENTRE AS LUTAS",
    "THE LOBBY'S OWNER CHOOSES WHO PLAYS": "O GESTOR DA SALA ESCOLHE QUEM LUTA",
    "COPIED: PASTE THE CODE TO THE OTHERS": "COPIADO: COLE O LOCALIZADOR PARA OS OUTROS",
    "COPIED: PASTE THE ADDRESS TO THE OTHERS": "COPIADO: COLE O ENDEREÇO PARA OS OUTROS",
    "PLAYERS  %d / %d": "VIAJANTES  %d / %d", "PLAYERS": "VIAJANTES", "SPECTATORS  %d / %d": "LISTA DE ESPERA  %d / %d",
    "%dP": "%dP", "OWNER": "GESTOR", "YOU": "VOCÊ", "READY": "CHECK-IN", "EMPTY": "VAGO",
    "LOBBY CODE": "LOCALIZADOR", "GIVE IT TO THE OTHERS": "PASSE PARA OS OUTROS", "THE OTHERS JOIN WITH IT": "OS OUTROS ENTRAM COM ELE",
    "PORT": "PORTA", "LOBBY": "SALA DE EMBARQUE", "JOINED BY ADDRESS": "CHECK-IN POR ENDEREÇO",
    "A MATCH IS ON": "LUTA EM CURSO", "TOURNAMENT ON": "RINHA EM CURSO", "TOURNAMENT OVER": "RINHA ENCERRADA",
    "RULES": "POLÍTICA", "RULES (THE OWNER'S)": "POLÍTICA (DO GESTOR)",
    "Opening the Lobby": "Abrindo a Sala", "Joining the Lobby": "Fazendo Check-in",
    "Looking for the lobby's owner...": "Procurando o gestor da sala...", "Opening the port...": "Abrindo a porta...",
    "Asking the matchmaking service for a code...": "Pedindo um localizador à central de reservas...",
    "No Longer in the Lobby": "Fora da Sala", "The lobby is closed.": "A sala foi fechada.",
    "Call Off the Tournament?": "Cancelar a Rinha?", "Its bracket is lost; the lobby is Versus again.": "O itinerário se perde; a sala volta para Versus.",
    "Close the Lobby?": "Fechar a Sala?", "Leave the Lobby?": "Sair da Sala?",
    "Everyone in it goes back to the menus.": "Todos nela voltam para os menus.", "You go back to the online menu.": "Você volta para o menu online.",
    "SAY SOMETHING TO THE LOBBY": "DIGA ALGO PARA A SALA",
    "Opening the lobby": "Abrindo a sala", "Joining the lobby": "Fazendo check-in na sala",
    "Close the lobby? ": "Fechar a sala? ", "Leave the lobby? ": "Sair da sala? ", "Call off the tournament? ": "Cancelar a rinha? ",
    "Player %d": "Viajante %d", "Spectator %d": "Lista de espera %d", "Move to ": "Remarcar para ", ", owner": ", gestor", ", ready": ", check-in feito",
    "Final": "Final", "Semifinal": "Semifinal", "Quarterfinal": "Quartas de final", "Back to the Lobby": "Voltar para a Sala",
    "%d ENTRANTS": "%d VIAJANTES", "BYE": "UPGRADE", "DISCONNECTED %d:%02d": "VOO ATRASADO %d:%02d", "LEFT": "NO-SHOW",
    "CHAMPION": "APROVADOR-GERAL", "FINAL": "FINAL",
    "THE TWO GAMES DISAGREE ON THIS MATCH: CHOOSE THE WINNER": "AS CONTAS DESTA LUTA NÃO BATEM: ESCOLHA O APROVADO",
    "THE OWNER IS CHOOSING THE WINNER": "O GESTOR ESTÁ ESCOLHENDO O APROVADO",
    "%s is the champion! Congratulations!": "%s é o Aprovador-Geral! Parabéns!",
    "The tournament is over.": "A rinha acabou.",
    "New Tournament draws another of the player places.": "Nova Rinha sorteia outra com quem está nos assentos.",
    "Waiting for the next match...": "Esperando a próxima luta...", "%s vs %s": "%s vs %s",
    "The two games disagree on this match: choose the winner.": "As contas desta luta não batem: escolha o aprovado.",
    "The owner is choosing the winner.": "O gestor está escolhendo o aprovado.",
    "Now: %s vs %s": "Agora: %s vs %s", "Next: %s vs %s": "Próxima: %s vs %s",
    "Waiting for %s to come back.": "Esperando %s voltar.", "Start it when both are here!": "Decole quando os dois estiverem aqui!",
    "The owner starts it.": "O gestor dá a decolagem.", "Choose the winner: ": "Escolha o aprovado: ",
    # the hosts' talk box
    "No code: the others type your address and this port on Join by Address. Left / right, L1 / R1 change it.":
        "Sem localizador: os outros digitam seu endereço e esta porta em Check-in por Endereço. Esquerda / direita, L1 / R1 mudam.",
    "Your lobby gets a code: give it to your friends, they type it on Join. Set the rules on its page!":
        "Sua sala ganha um localizador: passe para os colegas, eles digitam em Check-in. Defina a política na página dela!",
    "Enter the lobby's code, or Paste the one they sent you! Then Join: to play, or to watch from the stands.":
        "Digite o localizador da sala, ou Cole o que te mandaram! Depois Check-in: para lutar ou ficar na lista de espera.",
    "Type the lobby owner's address, and :port if it is not 7000. Then Join!":
        "Digite o endereço do gestor da sala, e :porta se não for 7000. Depois Check-in!",
    "You host! Your lobby's page shows the address and port the others type. Left / right, L1 / R1 change it.":
        "Você é o gestor! A página da sala mostra o endereço e a porta que os outros digitam. Esquerda / direita, L1 / R1 mudam.",
    "A friend hosts! Type the address and port their lobby's page shows, then Join!":
        "Um colega é o gestor! Digite o endereço e a porta que a página da sala dele mostra, depois Check-in!",
    "Here we go! The match is starting!": "Portas em automático! A luta vai decolar!",
    "Looking for a worthy challenger... Warm up in Training meanwhile!": "Procurando um desafiante à altura... Aqueça no Treino enquanto isso!",
    "Sorry! Quick match is for the game straight off the disc. Modded? Play your friends in a lobby!":
        "Foi mal! A reserva expressa é para o jogo direto do disco. Com mods? Lute com os colegas numa sala!",
    "Warm up at the tournament while we search! Pick yourself and a sparring partner.":
        "Aqueça na rinha enquanto buscamos! Escolha você e um parceiro de treino.",
    "Auto: rivals near you first. A region: players there first, everyone after 40 s. Anywhere: the fastest.":
        "Auto: rivais perto de você primeiro. Uma região: quem está lá primeiro, todos após 40 s. Qualquer lugar: o mais rápido.",
    "Pick a battle and we'll find you a rival! No time limit, and the game runs a little faster.":
        "Escolha uma luta e achamos um rival! Sem limite de tempo, e o jogo roda um pouco mais rápido.",
    "Pick your letters, then Done! Square deletes, Start finishes.": "Escolha as letras, depois Pronto! Quadrado apaga, Start conclui.",
    "Show everyone who you are! This is the name your rivals see.": "Mostre a todos quem você é! Este é o nome que seus rivais veem.",
    "Knocking at the lobby's door...": "Apresentando o cartão de embarque no portão...",
    "Opening your lobby! The others join it with your address.": "Abrindo sua sala! Os outros fazem check-in com o seu endereço.",
    "Opening your lobby! The others join it with its code.": "Abrindo sua sala! Os outros fazem check-in com o localizador.",
    "You are out of that lobby. OK takes you back to the online menu.": "Você saiu daquela sala. OK te leva de volta ao menu online.",
    "Type a line for everyone in the lobby, then Done! Start sends it too.": "Escreva uma linha para todos na sala, depois Pronto! Start também envia.",
    "Welcome to the lobby! Its owner picks who plays and starts the match. Chat while you wait!":
        "Bem-vindo à sala de embarque! O gestor escolhe quem luta e dá a decolagem. Converse enquanto espera!",
    "Tournament: Entrants sets the player places. Move people into them (you too, to play), then Start Tournament!":
        "Rinha: Viajantes define os assentos. Remarque as pessoas para eles (você também, para lutar), depois Começar a Rinha!",
    "Your lobby is open! Give the others its code: they type it on Join. Then put two of you in 1P and 2P.":
        "Sua sala está aberta! Passe o localizador: os outros digitam em Check-in. Depois coloque dois de vocês em 1P e 2P.",
    "Your lobby is open! The others join with your address and port (Join by Address). Then 1P and 2P.":
        "Sua sala está aberta! Os outros entram com seu endereço e porta (Check-in por Endereço). Depois 1P e 2P.",
    "New Lobby": "Nova Sala", "Searching": "Buscando", "Join by Address": "Check-in por Endereço", ", soon": ", em breve",
    "Your rival is here! The match begins.": "Seu rival embarcou! A luta vai começar.", " Your rival is from ": " Seu rival vem de ",
    "Quick match, teams: searching": "Reserva expressa, equipes: buscando",
    "Quick match, one on one: searching": "Reserva expressa, um contra um: buscando",
    "Joining lobby ": "Check-in na sala ", "Battle: teams": "Luta: equipes", "Battle: one on one": "Luta: um contra um",
    "Region: ": "Região: ", "You: ": "Você: ", "Partner: ": "Parceiro: ", "Start training": "Começar o treino",
    "No quick match. Quick match needs the original disc's data; with friends, both need the same data.":
        "Sem reserva expressa. Ela exige os dados originais do disco; com colegas, os dois precisam dos mesmos dados.",
    "Name: ": "Nome: ", "Code: ": "Localizador: ", "Address: ": "Endereço: ", "Chat: ": "Chat: ",
    "Searching for a rival...  %d:%02d": "Procurando um rival...  %d:%02d", "Quick Match: ": "Reserva Expressa: ",
    "PLAYER 1": "VIAJANTE 1", "PLAYER 2": "VIAJANTE 2", "SPLIT SCREEN": "TELA DIVIDIDA",
    "Spectating %s vs %s": "Assistindo %s vs %s", "CATCHING UP WITH THE MATCH... %d%%": "ALCANÇANDO A LUTA... %d%%",
    "NO LONGER THE PLAYERS' FIGHT (SINCE FRAME %d)": "JÁ NÃO É A LUTA DOS VIAJANTES (DESDE O QUADRO %d)",
    "VIEW: ": "VISÃO: ", "   R1: CHANGE   START: MENU": "   R1: TROCAR   START: MENU",
    "Player 2": "Viajante 2", "Split Screen": "Tela Dividida", "Player 1": "Viajante 1", "Stop Watching": "Parar de Assistir",
    "Close the Lobby": "Fechar a Sala", "Leave the Lobby": "Sair da Sala",
    "Your screen only: the fight goes on": "Só na sua tela: a luta continua", "Continue": "Continuar", "View: ": "Visão: ",
}


def parts(text):
    return re.findall(r"%%|%0?\d*[ds]", text), sorted(re.findall(r"\{\w+\}", text))


def main():
    r = subprocess.run([sys.executable, str(ROOT / "port/tools/tbo_strings.py"), "pt", "--name", "Português (Onfly)", "--out", str(OUT)],
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit(r.stdout + r.stderr)
    doc = json.loads(OUT.read_text(encoding="utf-8"))
    doc["name"] = "Português (Onfly)"
    seen, done = set(), 0
    for entry in doc["labels"].values():
        pt = PT.get(entry["original"])
        seen.add(entry["original"])
        if pt is None:
            continue
        if parts(pt) != parts(entry["original"]) or (entry["original"][:1] == " ") != (pt[:1] == " ") or (entry["original"][-1:] == " ") != (pt[-1:] == " "):
            sys.exit(f"FAILED {entry['original']!r}: the Portuguese line does not keep its %d / %s / {{name}} parts or its outer spaces")
        entry["text"] = pt
        done += 1
    OUT.write_text(json.dumps(doc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    gone = [k for k in PT if k not in seen]
    print(f"{OUT.relative_to(ROOT)}: {done} of {len(doc['labels'])} lines in Portuguese" + (f"; {len(gone)} lines below are no longer in the menus: {gone[:5]}" if gone else ""))


if __name__ == "__main__":
    main()
