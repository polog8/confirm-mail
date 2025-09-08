// Fichier : api/tracker.js

export default function handler(request, response) {
  // On récupère les paramètres de l'URL pour savoir quel e-mail a été ouvert
  const id = request.query.id || 'id_inconnu';
  const user = request.query.user || 'utilisateur_inconnu';

  // On affiche un message dans les logs de Vercel. 
  // C'est ici que vous verrez si l'e-mail a été ouvert.
  console.log(`✅ Email ouvert ! | ID: ${id} | User: ${user}`);

  // On crée les données binaires pour un GIF transparent de 1x1 pixel
  const gifData = Buffer.from(
    'R0lGODlhAQABAJAAAP8AAAAAACH5BAUQAAAALAAAAAABAAEAAAICRAEAOw==',
    'base64'
  );

  // On envoie les en-têtes HTTP pour dire que c'est une image
  response.setHeader('Content-Type', 'image/gif');
  response.setHeader('Cache-Control', 'no-store, no-cache, must-revalidate, proxy-revalidate');

  // On envoie l'image et on termine la requête
  response.status(200).send(gifData);
}
