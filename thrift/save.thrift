namespace cpp save_service

service Save {

    /**
     * username: name of save data server
     * password: First 8 letter of the md5sum of the password of save data server
     * reslut will be save in save data server result.txt
     */
    i32 save_data(1: string username, 2: string password, 3: i32 player1_id, 4: i32 player2_id)
}
