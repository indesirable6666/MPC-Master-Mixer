# MPC Live II — collecte de diagnostic en lecture seule

Le script [collect-mpc-audit.sh](../tools/diagnostics/collect-mpc-audit.sh)
collecte les éléments nécessaires pour confirmer le profil runtime avant de
choisir un hôte natif ou un transport IPC.

## Exécution

1. Récupérer le script depuis la branche du projet et le copier sur une clé USB.
2. Insérer la clé et ouvrir un terminal sur la MPC par le moyen de développement
   déjà autorisé.
3. Lancer le script avec le shell disponible, sans privilèges supplémentaires :

   ```sh
   sh /chemin/vers/collect-mpc-audit.sh
   ```

4. Le rapport est écrit dans
   `/media/az01-internal/mpc-mixer-audit-auto.txt`.
5. Lire le rapport et vérifier son contenu avant de le partager.

Le script ne modifie ni le root filesystem, ni les services, ni le binaire MPC,
ni les réglages audio. Il ne crée pas de socket, n'ouvre pas de port, ne lance
pas de test audio/MIDI et n'initialise aucun périphérique graphique. Il lit
principalement `/proc`, quelques fichiers d'identification système et les
binaires déjà présents pour en extraire des chaînes.

## Ce que le rapport doit permettre de confirmer

- Version du noyau et identité Buildroot exposée par le système.
- Version de libc lorsque `getconf` est disponible, et présence éventuelle des
  chemins de libc/loader courants.
- PID réellement détecté pour MPC et bibliothèques mappées par ce processus
  (sans supposer un PID fixe).
- Présence de `CONFIG_UNIX` si la configuration du noyau est exposée.
- Tables TCP/UDP et sockets Unix déjà visibles, sans sonder les ports.
- Outils et utilitaires disponibles sur l'image.

Une valeur absente signifie seulement que l'information n'est pas exposée par
les chemins/outils testés ; ce n'est pas la preuve que la fonctionnalité
correspondante est absente.

## Confidentialité et limites

Le rapport peut contenir des arguments de processus, chemins de bibliothèques,
ports, adresses réseau et noms de services. Le relire et masquer les éléments
privés avant de le transmettre. L'accès à `/proc/<pid>/fd` et `maps` peut être
refusé selon l'identité du processus et les permissions.

La collecte ne prouve pas qu'une application tierce peut être lancée, qu'elle
peut afficher une fenêtre, partager le tactile, ou coexister avec le propriétaire
DRM de l'écran. Elle ne valide pas non plus un SDK de compilation. Ces points
nécessitent des preuves distinctes et une autorisation explicite.

Ne pas utiliser ce rapport comme autorisation de modifier le système. Aucun
changement de démarrage, service, routage ALSA, activation, licence ou rootfs
n'est couvert par cette procédure.
