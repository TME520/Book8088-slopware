/* rosary_text.c - the single source of the rosary texts (LPS-DOS-115-CHG03b).
 * Linked only into tools/mkrosary (which writes ROSARY.DAT) and the host tests.
 * LPSCOMP.EXE does not contain these strings; it reads ROSARY.DAT at run time.
 * Accents stay ASCII-folded for now. */
#include "rosary.h"

static const char *const PRAYERS[ROS_T_PRAYER_COUNT][2]={
    /* ROS_T_SIGN */
    {"In the name of the Father, and of the Son, and of the Holy Spirit. Amen.",
     "Au nom du Pere, du Fils et du Saint-Esprit. Amen."},
    /* ROS_T_CREED */
    {"I believe in God, the Father almighty, Creator of heaven and earth, and in Jesus Christ, his only Son, our Lord, who was conceived by the Holy Spirit, born of the Virgin Mary, suffered under Pontius Pilate, was crucified, died and was buried; he descended into hell; on the third day he rose again from the dead; he ascended into heaven, and is seated at the right hand of God the Father almighty; from there he will come to judge the living and the dead. I believe in the Holy Spirit, the holy catholic Church, the communion of saints, the forgiveness of sins, the resurrection of the body, and life everlasting. Amen.",
     "Je crois en Dieu, le Pere tout-puissant, createur du ciel et de la terre. Et en Jesus-Christ, son Fils unique, notre Seigneur, qui a ete concu du Saint-Esprit, est ne de la Vierge Marie, a souffert sous Ponce Pilate, a ete crucifie, est mort et a ete enseveli, est descendu aux enfers. Le troisieme jour est ressuscite des morts, est monte aux cieux, est assis a la droite de Dieu le Pere tout-puissant, d'ou il viendra juger les vivants et les morts. Je crois en l'Esprit Saint, a la sainte Eglise catholique, a la communion des saints, a la remission des peches, a la resurrection de la chair, a la vie eternelle. Amen."},
    /* ROS_T_OUR_FATHER */
    {"Our Father, who art in heaven, hallowed be thy name; thy kingdom come; thy will be done on earth as it is in heaven. Give us this day our daily bread; and forgive us our trespasses, as we forgive those who trespass against us; and lead us not into temptation, but deliver us from evil. Amen.",
     "Notre Pere, qui es aux cieux, que ton nom soit sanctifie, que ton regne vienne, que ta volonte soit faite sur la terre comme au ciel. Donne-nous aujourd'hui notre pain de ce jour. Pardonne-nous nos offenses, comme nous pardonnons aussi a ceux qui nous ont offenses. Et ne nous laisse pas entrer en tentation, mais delivre-nous du Mal. Amen."},
    /* ROS_T_HAIL */
    {"Hail Mary, full of grace, the Lord is with thee. Blessed art thou among women, and blessed is the fruit of thy womb, Jesus. Holy Mary, Mother of God, pray for us sinners, now and at the hour of our death. Amen.",
     "Je vous salue, Marie, pleine de grace ; le Seigneur est avec vous. Vous etes benie entre toutes les femmes, et Jesus, le fruit de vos entrailles, est beni. Sainte Marie, Mere de Dieu, priez pour nous pauvres pecheurs, maintenant et a l'heure de notre mort. Amen."},
    /* ROS_T_GLORY */
    {"Glory be to the Father, and to the Son, and to the Holy Spirit, as it was in the beginning, is now, and ever shall be, world without end. Amen.",
     "Gloire au Pere, au Fils et au Saint-Esprit, comme il etait au commencement, maintenant et toujours, pour les siecles des siecles. Amen."},
    /* ROS_T_SILENCE */
    {"Remain in silence. Place this mystery, your intentions and your whole attention before God. Press Enter when you are ready to continue.",
     "Demeurez en silence. Placez ce mystere, vos intentions et toute votre attention devant Dieu. Appuyez sur Entree lorsque vous etes pret a continuer."},
    /* ROS_T_FATIMA */
    {"O my Jesus, forgive us our sins, save us from the fires of hell, lead all souls to heaven, especially those most in need of thy mercy. Amen.",
     "O mon Jesus, pardonnez-nous nos peches, preservez-nous du feu de l'enfer et conduisez au ciel toutes les ames, surtout celles qui ont le plus besoin de votre misericorde. Amen."},
    /* ROS_T_HHQ */
    {"Hail, holy Queen, Mother of mercy, our life, our sweetness and our hope. To thee do we cry, poor banished children of Eve. To thee do we send up our sighs, mourning and weeping in this valley of tears. Turn then, most gracious advocate, thine eyes of mercy toward us, and after this our exile show unto us the blessed fruit of thy womb, Jesus. O clement, O loving, O sweet Virgin Mary. Pray for us, O holy Mother of God, that we may be made worthy of the promises of Christ.",
     "Salut, o Reine, Mere de misericorde, notre vie, notre douceur et notre esperance, salut. Enfants d'Eve exiles, nous crions vers vous. Vers vous nous soupirons, gemissant et pleurant dans cette vallee de larmes. O vous, notre avocate, tournez vers nous vos regards misericordieux. Et apres cet exil, montrez-nous Jesus, le fruit beni de vos entrailles. O clemente, o misericordieuse, o douce Vierge Marie. Priez pour nous, sainte Mere de Dieu, afin que nous devenions dignes des promesses du Christ."},
    /* ROS_T_CLOSING */
    {"O God, whose only-begotten Son, by his life, death and resurrection, has purchased for us the rewards of eternal life: grant that, meditating upon these mysteries of the holy Rosary, we may imitate what they contain and obtain what they promise, through Christ our Lord. Amen.",
     "O Dieu, dont le Fils unique, par sa vie, sa mort et sa resurrection, nous a acquis les recompenses de la vie eternelle, accordez-nous, en meditant ces mysteres du tres saint Rosaire, d'imiter ce qu'ils contiennent et d'obtenir ce qu'ils promettent, par le Christ notre Seigneur. Amen."},
};
static const char *const MYSTERY_EN[4][5]={
        {"The Annunciation","The Visitation","The Nativity","The Presentation","Finding Jesus in the Temple"},
        {"The Baptism of Jesus","The Wedding at Cana","Proclamation of the Kingdom","The Transfiguration","Institution of the Eucharist"},
        {"The Agony in the Garden","The Scourging at the Pillar","The Crowning with Thorns","The Carrying of the Cross","The Crucifixion"},
        {"The Resurrection","The Ascension","The Descent of the Holy Spirit","The Assumption of Mary","The Coronation of Mary"}};
static const char *const MYSTERY_FR[4][5]={
        {"L'Annonciation","La Visitation","La Nativite","La Presentation","Jesus retrouve au Temple"},
        {"Le Bapteme de Jesus","Les Noces de Cana","L'annonce du Royaume","La Transfiguration","L'institution de l'Eucharistie"},
        {"L'Agonie au Jardin","La Flagellation","Le Couronnement d'epines","Le Portement de la Croix","La Crucifixion"},
        {"La Resurrection","L'Ascension","La Pentecote","L'Assomption de Marie","Le Couronnement de Marie"}};
static const char *const SCRIPTURE_EN[4][5]={
        {"Luke 1:38 - Let it be to me according to your word.","Luke 1:42 - Blessed are you among women.","Luke 2:7 - She gave birth to her firstborn son.","Luke 2:30 - My eyes have seen your salvation.","Luke 2:49 - I must be in my Father's house."},
        {"Matthew 3:17 - This is my beloved Son.","John 2:5 - Do whatever he tells you.","Mark 1:15 - Repent and believe in the Gospel.","Matthew 17:2 - His face shone like the sun.","Luke 22:19 - This is my body, given for you."},
        {"Luke 22:42 - Not my will, but yours be done.","John 19:1 - Pilate took Jesus and had him scourged.","Matthew 27:29 - They placed a crown of thorns on his head.","Luke 23:26 - They laid the cross on Simon.","Luke 23:46 - Father, into your hands I commend my spirit."},
        {"Matthew 28:6 - He is not here; he has risen.","Acts 1:9 - He was lifted up before their eyes.","Acts 2:4 - They were all filled with the Holy Spirit.","Luke 1:49 - The Almighty has done great things for me.","Revelation 12:1 - A woman clothed with the sun."}};
static const char *const SCRIPTURE_FR[4][5]={
        {"Luc 1,38 - Qu'il me soit fait selon ta parole.","Luc 1,42 - Tu es benie entre les femmes.","Luc 2,7 - Elle mit au monde son fils premier-ne.","Luc 2,30 - Mes yeux ont vu ton salut.","Luc 2,49 - Je dois etre chez mon Pere."},
        {"Matthieu 3,17 - Celui-ci est mon Fils bien-aime.","Jean 2,5 - Faites tout ce qu'il vous dira.","Marc 1,15 - Convertissez-vous et croyez a l'Evangile.","Matthieu 17,2 - Son visage devint brillant comme le soleil.","Luc 22,19 - Ceci est mon corps donne pour vous."},
        {"Luc 22,42 - Non pas ma volonte, mais la tienne.","Jean 19,1 - Pilate fit flageller Jesus.","Matthieu 27,29 - Ils poserent sur sa tete une couronne d'epines.","Luc 23,26 - Ils chargerent Simon de la croix.","Luc 23,46 - Pere, entre tes mains je remets mon esprit."},
        {"Matthieu 28,6 - Il n'est pas ici, il est ressuscite.","Actes 1,9 - Il s'eleva sous leurs yeux.","Actes 2,4 - Tous furent remplis de l'Esprit Saint.","Luc 1,49 - Le Puissant fit pour moi des merveilles.","Apocalypse 12,1 - Une femme vetue du soleil."}};
static const char *const MEDITATION_EN[4][5]={
        {"Ask for Mary's trust: receive God's call without fear.","Carry Christ toward another person with humble joy.","Welcome Jesus in poverty, simplicity and gratitude.","Offer to God what is most precious, without possessing it.","Seek Christ patiently whenever he seems absent."},
        {"Remember your baptism and live today as a beloved child of God.","Entrust the ordinary needs of life to Christ and obey him.","Let Christ rule first within your own heart.","Ask to see Christ's light behind present difficulties.","Receive Christ's self-gift and learn to give yourself."},
        {"Bring anguish to the Father and choose his will in trust.","Pray for endurance when suffering is undeserved.","Reject pride and contemplate the quiet kingship of Christ.","Carry today's burden beside Jesus, one step at a time.","Remain at the Cross and receive the mercy flowing from it."},
        {"Let the risen Christ awaken hope where life seems closed.","Lift your heart toward heaven while serving faithfully on earth.","Ask the Holy Spirit for courage, wisdom and charity.","Entrust your whole life to God as Mary did.","Contemplate the dignity promised to a life united with God."}};
static const char *const MEDITATION_FR[4][5]={
        {"Demandez la confiance de Marie : accueillir l'appel de Dieu sans peur.","Portez le Christ vers autrui avec une joie humble.","Accueillez Jesus dans la pauvrete, la simplicite et la gratitude.","Offrez a Dieu ce qui est precieux sans chercher a le posseder.","Cherchez patiemment le Christ lorsqu'il semble absent."},
        {"Souvenez-vous de votre bapteme et vivez en enfant aime de Dieu.","Confiez au Christ les besoins ordinaires et faites ce qu'il dit.","Laissez d'abord le Christ regner dans votre propre coeur.","Demandez a voir la lumiere du Christ derriere les difficultes.","Recevez le don du Christ et apprenez a vous donner."},
        {"Portez votre angoisse au Pere et choisissez sa volonte avec confiance.","Demandez la force d'endurer une souffrance immeritee.","Rejetez l'orgueil et contemplez la royaute silencieuse du Christ.","Portez le fardeau du jour aupres de Jesus, pas apres pas.","Demeurez au pied de la Croix et recevez sa misericorde."},
        {"Que le Christ ressuscite reveille l'esperance la ou tout semble ferme.","Elevez votre coeur vers le ciel en servant fidelement sur terre.","Demandez a l'Esprit Saint courage, sagesse et charite.","Confiez toute votre vie a Dieu comme Marie.","Contemplez la dignite promise a une vie unie a Dieu."}};

/* id: ROS_T_* (see rosary.h). Returns 0 for an unknown id. */
const char *rosary_source_text(unsigned id, int lang){
    int fr=lang==LANG_FR;
    if(id<ROS_T_PRAYER_COUNT) return PRAYERS[id][fr];
    if(id<ROS_T_SCRIPTURE){ id-=ROS_T_MYSTERY; return fr?MYSTERY_FR[id/5][id%5]:MYSTERY_EN[id/5][id%5]; }
    if(id<ROS_T_MEDITATION){ id-=ROS_T_SCRIPTURE; return fr?SCRIPTURE_FR[id/5][id%5]:SCRIPTURE_EN[id/5][id%5]; }
    if(id<ROS_T_COUNT){ id-=ROS_T_MEDITATION; return fr?MEDITATION_FR[id/5][id%5]:MEDITATION_EN[id/5][id%5]; }
    return 0;
}
